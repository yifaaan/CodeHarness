# Skill System

**Source**: `packages/agent-core/src/skill/`

## Purpose

Skills are **reusable Markdown documents** that encode domain knowledge, workflows, or instructions. They're injected into the agent's system prompt when activated, providing context-specific guidance without needing to paste the same instructions repeatedly.

A Skill is like a "plugin for the agent's brain" — it changes what the agent knows about a specific topic or workflow.

## Skill Architecture

```
SkillRegistry (session-level)
│
├── loadRoots(roots)        → Scan directories for skill files
├── getSkill(name)          → Find skill by name
├── listSkills()            → All discovered skills
├── renderSkillPrompt(skill, args)  → Render skill with arguments
│
├── Skill sources:
│   ├── builtin/            → Bundled with the CLI (e.g., mcp-config)
│   ├── user/               → ~/.kimi-code/skills/, ~/.agents/skills/
│   ├── project/            → .kimi-code/skills/, .agents/skills/
│   └── extra/              → Configured via config.toml extra_skill_dirs
│
└── SkillManager (agent-level)
    └── activate(payload)   → Apply skill to current conversation
```

## Skill Definition

```typescript
interface SkillDefinition {
  name: string;                          // Skill name (from frontmatter or filename)
  description: string;                   // Description for LLM to decide when to use
  path: string;                          // Full path to skill file/directory
  dir: string;                           // Directory containing the skill
  content: string;                       // Rendered Markdown body
  metadata: SkillMetadata;               // Frontmatter fields
  source: SkillSource;                   // Where this skill came from
}

interface SkillMetadata {
  name: string;
  description: string;
  type: 'prompt' | 'inline' | 'flow';   // Skill type
  whenToUse?: string;                    // Guidance for LLM on when to auto-invoke
  disableModelInvocation?: boolean;      // If true, only user can invoke (no auto)
  arguments?: string[];                  // Expected arguments
  model?: string;                        // Optional model override
}
```

### Skill Types

| Type | Description | Invocation |
|------|-------------|------------|
| `prompt` | Injected as system prompt | Slash command or model auto-invokes |
| `inline` | Runs inline in conversation | Slash command only |
| `flow` | Multi-step workflow | (Not fully implemented) |

### Skill File Formats

**Directory form (recommended)**:
```
<skill-name>/
├── SKILL.md           # Main skill content (YAML frontmatter + Markdown)
├── script.sh          # Optional helper scripts
└── references/        # Optional reference files
```

**Flat file form**:
```
<skill-name>.md         # Self-contained skill file
```

Discovery priority: if both `<name>/SKILL.md` and `<name>.md` exist, the directory form wins.

## Skill File Example

```markdown
---
name: code-style
description: Applies project code style conventions
type: prompt
whenToUse: When writing, modifying, or reviewing source code
disableModelInvocation: false
arguments:
  - target
  - mode
---

# Code Style Guide

Please follow these conventions:

## Naming
- Variables: camelCase
- Types/Classes: PascalCase  
- Functions: camelCase
- Constants: UPPER_SNAKE_CASE
- Files: kebab-case

## Formatting
- Indent: 2 spaces
- Max line length: 100 characters
- Semicolons: required

## Comments
- Public API functions: TSDoc comments
- Internal functions: inline comments for non-obvious logic
- No TODO comments in committed code
```

## Frontmatter Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Unique skill name (used for `/name` invocation) |
| `description` | string | Yes | Describes when the LLM should auto-invoke |
| `type` | string | No | `prompt` (default), `inline`, `flow` |
| `whenToUse` | string | No | Context hints for LLM auto-invocation |
| `disableModelInvocation` | bool | No | `false` (default) = model can auto-invoke |
| `arguments` | string[] | No | Expected positional arguments |
| `model` | string | No | Override model for this skill |

## Skill Parsing

**Source**: `packages/agent-core/src/skill/parser.ts`

```typescript
class SkillParser {
  /** Parse a skill file content into SkillDefinition */
  static parse(content: string, filePath: string): SkillDefinition;

  /** Parse YAML frontmatter */
  static parseFrontmatter(content: string): SkillMetadata;

  /** Extract body (everything after frontmatter) */
  static parseBody(content: string): string;
}
```

Parsing algorithm:
1. Read file content
2. Check for YAML frontmatter between `---` fences
3. Parse frontmatter with YAML parser
4. Extract body (Markdown after `---`)
5. Return `SkillDefinition { name, description, content, metadata, ... }`

## Parameter Expansion

Skills support variable substitution in their content:

```typescript
function renderSkillPrompt(skill: SkillDefinition, rawArgs: string): string {
  const args = rawArgs.split(/\s+/);
  
  let content = skill.content;
  
  // $ARGUMENTS = raw arguments string
  content = content.replace(/\$ARGUMENTS/g, rawArgs);
  
  // $0, $1, $2... = positional arguments
  args.forEach((arg, i) => {
    content = content.replace(new RegExp(`\\$${i}`, 'g'), arg);
  });
  
  // Named arguments: $name, $target, etc.
  // From skill.metadata.arguments mapping
  skill.metadata.arguments?.forEach((key, i) => {
    content = content.replace(new RegExp(`\\$${key}`, 'g'), args[i] || '');
  });
  
  // Context variables:
  // ${KIMI_SKILL_DIR} = path to the skill's directory
  content = content.replace(/\$\{KIMI_SKILL_DIR\}/g, skill.dir);
  
  // ${KIMI_SESSION_ID} = current session ID
  content = content.replace(/\$\{KIMI_SESSION_ID\}/g, currentSessionId);
  
  return content;
}
```

## Skill Discovery

**Source**: `packages/agent-core/src/skill/scanner.ts`

```typescript
class SkillScanner {
  /** Scan directories for skill files */
  static scan(roots: SkillRoot[]): AsyncIterable<SkillDefinition>;

  /** Resolve skill root directories */
  static resolveRoots(config: SkillConfig): SkillRoot[];
}
```

### Discovery Algorithm

```
resolveSkillRoots(config):
  roots = []
  
  // 1. Explicit directories (if configured via --skills-dir or extra_skill_dirs)
  if config.explicitDirs:
    roots += config.explicitDirs.map(d => ({ path: d, source: 'extra' }))
  
  // 2. Project-level directories
  roots += [
    { path: '<cwd>/.kimi-code/skills', source: 'project' },
    { path: '<cwd>/.agents/skills', source: 'project' },
  ]
  
  // 3. User-level directories
  roots += [
    { path: '~/.kimi-code/skills', source: 'user' },
    { path: '~/.agents/skills', source: 'user' },
  ]
  
  // 4. Built-in skills (bundled with CLI)
  roots += [{ path: '<app>/builtin-skills', source: 'builtin' }]
  
  return roots

scan(root):
  for each entry in root path:
    if entry is <name>/SKILL.md:
      → Parse as skill bundle (do NOT descend further)
    elif entry is <name>.md:
      → Parse as flat skill file
    else:
      → Skip (node_modules, dotfiles, etc.)
```

### Priority (Name Collision)

When two skills have the same name, the first one found wins:

```
project  >  user  >  extra  >  builtin
```

This means a project-level skill can override a built-in skill with the same name.

### Ignored Paths

- `node_modules/` directories
- Dotfiles (`.hidden`) and dot-directories
- Files more than 8 levels deep
- Non-`.md` files at the root level

## SkillRegistry

**Source**: `packages/agent-core/src/skill/registry.ts`

```typescript
class SkillRegistry {
  constructor();

  /** Load skill roots (scans directories, parses files) */
  async loadRoots(roots: SkillRoot[]): Promise<void>;

  /** Register a single skill (for built-in skills) */
  register(skill: SkillDefinition): void;

  /** Get skill by name */
  getSkill(name: string): SkillDefinition | undefined;

  /** Render skill content with argument expansion */
  renderSkillPrompt(skill: SkillDefinition, rawArgs: string): string;

  /** List all skills */
  listSkills(): readonly SkillDefinition[];

  /** List only skills that can be invoked (not disabled, not inline-only) */
  listInvocableSkills(): readonly SkillDefinition[];

  /** Remove all skills from a specific source */
  removeSource(source: SkillSource): void;
}

type SkillSource = 'project' | 'user' | 'extra' | 'builtin';
```

## SkillManager (Agent-Level)

**Source**: `packages/agent-core/src/agent/skill/`

```typescript
class SkillManager {
  constructor(agent: Agent);

  /** Activate a skill */
  async activate(payload: SkillActivationPayload): Promise<void>;
}

interface SkillActivationPayload {
  name: string;
  args: string;
  origin: 'user-slash' | 'model-tool' | 'nested-skill';
}
```

### Activation Flow

```
SkillManager.activate({ name: "code-style", args: "src/main.ts", origin: "model-tool" })
  1. Look up skill in SkillRegistry:
     skill = registry.getSkill("code-style")
     if not found → error: "Skill not found"
  
  2. Render skill content with args:
     prompt = registry.renderSkillPrompt(skill, "src/main.ts")
     → "$0" replaced with "src/main.ts"
  
  3. Append skill content as user message:
     agent.context.appendUserMessage(
       [{ type: 'text', text: prompt }],
       { origin: 'skill_activation' }
     )
  
  4. Record skill activation:
     agent.records.logRecord({
       type: 'skill.invoked',
       name: "code-style",
       args: "src/main.ts",
     })
  
  5. Trigger skill-invoked event:
     agent.emitEvent({ type: 'skill.invoked', skillName: "code-style" })
```

### Activation Origins

| Origin | How | Example |
|--------|-----|---------|
| `user-slash` | User types `/<skill-name>` | `/code-style src/main.ts` |
| `model-tool` | LLM calls Skill tool | Model decides to invoke skill |
| `nested-skill` | Skill calls another skill | Recursive activation (max depth: 3) |

## Built-in Skills

**Source**: `packages/agent-core/src/skill/builtin/`

The CLI bundles built-in skills. Currently one:

### `/mcp-config` Skill

Provides an interactive MCP configuration flow. When invoked, it:
1. Lists current MCP servers
2. Walks user through adding/editing/deleting servers
3. Manages OAuth login for MCP servers
4. Saves changes to `mcp.json`

## Slash Command Integration

Skills appear as slash commands in the TUI:

```
Type "/" in the input box:
  → Built-in commands: /help, /new, /model, /plan, ...
  → Skill commands: /code-style, /review-pr, /commit-message, ...

If built-in command and skill share a name:
  → Skill version requires /skill:<name>
```

When a user types `/<skill-name>`, the TUI sends it to the agent, which routes it to `SkillManager.activate()`.

## Re-implementation Notes

1. **Skills are just Markdown + YAML**: No code execution, no complex logic. A skill parser needs only YAML frontmatter parsing and Markdown body extraction.

2. **Scanning is filesystem-based**: Walk directories, find `SKILL.md` or `.md` files, parse frontmatter. The priority system (project > user > extra > builtin) is important for name collision resolution.

3. **Variable substitution is string replacement**: `$0`, `$1`, `$name`, `${KIMI_SKILL_DIR}` are simple `string.replace()` calls. No template engine needed beyond this.

4. **Skill content becomes a user message**: When activated, the rendered skill content is appended as a user message with `origin: 'skill_activation'`. This means the LLM sees it as part of the conversation, not as a system prompt override.

5. **Recursion limit of 3**: The `SkillTool` or nested skill activation checks `MAX_SKILL_QUERY_DEPTH = 3`. This prevents infinite skill invocation loops.

6. **disableModelInvocation field**: When `true`, the LLM won't auto-invoke this skill (the Skill tool is filtered out). Users can still invoke via `/skill-name`.