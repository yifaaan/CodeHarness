# Skill System

Reusable Markdown documents that encode domain knowledge.

## One-Liner

Skills are Markdown files with YAML frontmatter that inject domain-specific guidance into the agent's context when activated.

## Skill Definition

```typescript
interface SkillDefinition {
  name: string;
  description: string;
  path: string;
  content: string;
  metadata: SkillMetadata;
  source: SkillSource;
}

interface SkillMetadata {
  name: string;
  description: string;
  type: 'prompt' | 'inline' | 'flow';
  whenToUse?: string;
  disableModelInvocation?: boolean;
  arguments?: string[];
  model?: string;
}
```

## Skill Types

| Type | Description | Invocation |
|------|-------------|------------|
| `prompt` | Injected as system prompt | Slash command or auto |
| `inline` | Runs inline | Slash command only |
| `flow` | Multi-step workflow | (Not fully implemented) |

## Skill File Format

```markdown
---
name: code-style
description: Applies project code style conventions
type: prompt
whenToUse: When writing or reviewing code
arguments:
  - target
  - mode
---

# Code Style Guide

Please follow these conventions:
- Variables: camelCase
- Types: PascalCase
- Functions: camelCase
```

## Skill Discovery

Priority (first wins):
```
project > user > extra > builtin
```

### Discovery Roots

```
1. Explicit: extra_skill_dirs config
2. Project: <cwd>/.kimi-code/skills, <cwd>/.agents/skills
3. User: ~/.kimi-code/skills, ~/.agents/skills
4. Built-in: bundled with CLI
```

## Parameter Expansion

```typescript
// $ARGUMENTS = raw arguments string
// $0, $1, $2... = positional arguments
// $name = named argument
// ${KIMI_SKILL_DIR} = skill directory
// ${KIMI_SESSION_ID} = session ID
```

## SkillRegistry

```typescript
class SkillRegistry {
  async loadRoots(roots: SkillRoot[]): Promise<void>;
  register(skill: SkillDefinition): void;
  getSkill(name: string): SkillDefinition | undefined;
  renderSkillPrompt(skill: SkillDefinition, rawArgs: string): string;
  listSkills(): readonly SkillDefinition[];
  listInvocableSkills(): readonly SkillDefinition[];
}
```

## SkillManager

```typescript
class SkillManager {
  async activate(payload: SkillActivationPayload): Promise<void>;
}

interface SkillActivationPayload {
  name: string;
  args: string;
  origin: 'user-slash' | 'model-tool' | 'nested-skill';
}
```

## Activation Flow

```
SkillManager.activate({ name, args, origin })
    1. Look up skill in SkillRegistry
    2. Render skill content with args (parameter expansion)
    3. Append skill content as user message
    4. Record skill.invoked event
    5. Emit skill.invoked event
```

## Slash Commands

Skills appear as `/skill-name` commands:

```
Type "/" in input box:
    → Built-in: /help, /new, /model, /plan, ...
    → Skills: /code-style, /review-pr, ...

If conflict with built-in:
    → Use /skill:<name>
```

## Recursion Limit

`MAX_SKILL_QUERY_DEPTH = 3`

Prevents infinite skill invocation loops.

## Built-in Skills

| Skill | Purpose |
|-------|---------|
| `/mcp-config` | Interactive MCP configuration |

## See Also

- [agent-lifecycle.md](agent-lifecycle.md) — SkillManager integration
- [tool-system.md](tool-system.md) — Skill tool
