# Tool System

**Source**: `packages/agent-core/src/tools/`

## Purpose

The tool system defines **everything the agent can do** — reading/writing files, executing shell commands, searching the web, spawning subagents, asking the user questions, managing background tasks, and more. Each tool is a self-contained module with a JSON Schema for its parameters, an execution function, and optional permission policies.

## Tool Architecture

```
ToolManager
│
├── builtinTools: ExecutableTool[]     — Tools shipped with the CLI
│   ├── file/                           — Read, Write, Edit, Glob, Grep, ReadMediaFile
│   ├── shell/                          — Bash
│   ├── web/                            — FetchURL, WebSearch
│   ├── collaboration/                  — Agent, AskUserQuestion, Skill
│   ├── planning/                       — EnterPlanMode, ExitPlanMode
│   ├── state/                          — TodoList
│   └── background/                     — TaskCreate, TaskList, TaskOutput, TaskStop
│
├── userTools: ExecutableTool[]        — Dynamically registered tools (via SDK)
├── mcpTools: ExecutableTool[]         — Tools from MCP servers
│
└── activeTools: Set<string>           — Names of currently active tools
```

## ExecutableTool Interface

**Source**: `packages/agent-core/src/loop/types.ts`

```typescript
interface ExecutableTool<Input = unknown> {
  /** Unique tool name (used by LLM to call the tool) */
  readonly name: string;
  
  /** Human-readable description (LLM uses this to decide when to call) */
  readonly description: string;
  
  /** JSON Schema for tool parameters */
  readonly parameters: JSONSchema;
  
  /**
   * Phase 1: Validate args and return execution metadata.
   * This is a PURE function — no side effects.
   * Called BEFORE permission check.
   */
  resolveExecution(args: Input, context: ToolExecutionMetadata): ToolExecution;
}

interface ToolExecution {
  /** Human-readable description of what this execution will do */
  readonly description: string;
  
  /** What resources this tool accesses (for permission checking) */
  readonly accesses?: ToolAccesses;
  
  /**
   * Phase 2: Execute the tool.
   * This IS side-effectful.
   * Called AFTER permission check approves.
   */
  execute(context: ExecutableToolContext): Promise<ToolResult>;
}

interface ToolExecutionMetadata {
  turnId: string;
  toolCallId: string;
  agent: { type: 'main' | 'sub' | 'independent' };
}

interface ExecutableToolContext {
  turnId: string;
  toolCallId: string;
  signal: AbortSignal;
  onUpdate: (update: ToolUpdate) => void;  // Stream progress updates
}

type ToolResult = 
  | { type: 'text'; text: string }
  | { type: 'error'; message: string; code?: string }
  | { type: 'structured'; value: unknown };

interface ToolUpdate {
  type: 'stdout' | 'stderr' | 'progress' | 'status' | 'custom';
  content: string;
  metadata?: Record<string, unknown>;
}

interface ToolAccesses {
  readFiles?: boolean;
  writeFiles?: boolean;
  network?: boolean;
  executeCommands?: boolean;
  manageAgents?: boolean;
  manageTasks?: boolean;
}
```

## ToolManager

**Source**: `packages/agent-core/src/agent/tool/index.ts`

```typescript
class ToolManager {
  constructor(agent: Agent, config: ToolManagerConfig);

  /** Initialize built-in tools */
  initializeBuiltinTools(): void;

  /** Set which tools are active (by name) */
  setActiveTools(names: string[]): void;

  /** Register a user-defined tool */
  registerUserTool(definition: UserToolDefinition): void;

  /** Unregister a user-defined tool */
  unregisterUserTool(name: string): void;

  /** Register MCP tools from a connected server */
  registerMcpServer(serverName: string, tools: MCPToolDefinition[]): void;

  /** Unregister MCP tools from a disconnected server */
  unregisterMcpServer(serverName: string): void;

  /** Get the active tool set for LLM calls */
  get loopTools(): ExecutableTool[];

  /** Get all tools (for display/debug) */
  getAllTools(): ToolInfo[];

  /** Find a specific tool by name */
  getTool(name: string): ExecutableTool | undefined;
}

interface ToolInfo {
  name: string;
  source: 'builtin' | 'user' | 'mcp';
  mcpServer?: string;
  description: string;
}
```

## Built-in Tools

### File Tools

**Source**: `packages/agent-core/src/tools/builtin/file/`

#### ReadTool
```
name: "Read"
description: "Read file contents with line numbers"
parameters:
  path: string        # File path (required)
  line_offset?: int   # Starting line (1-based, negative = from end)
  n_lines?: int       # Max lines to read (default: all)
  
behavior:
  - Maximum 1000 lines or 100KB per call
  - Negative line_offset reads from end: line_offset=-1 reads last line
  - Detects binary files (NUL bytes) and rejects them
  - Returns content with line numbers: "<line-num>\t<content>"
  - Handles CRLF/LF line endings (converts CRLF to LF for display)
  - Uses Kaos to read the file
```

#### WriteTool
```
name: "Write"
description: "Create or overwrite a file"
parameters:
  path: string        # File path (required)
  content: string     # File content (required)
  mode?: "overwrite" | "append"   # Default: overwrite
  
behavior:
  - Creates file (parent directory must exist)
  - "append" mode: appends content to end of file (no auto newline)
  - Returns number of UTF-8 bytes written
  - Uses Kaos to write the file
```

#### EditTool
```
name: "Edit"
description: "Replace exact text in a file"
parameters:
  file_path: string       # File path (required)
  old_string: string      # Text to replace (required)
  new_string: string      # Replacement text (required, must differ)
  replace_all?: boolean   # Replace all occurrences (default: false)
  
behavior:
  - Exact string replacement (not regex)
  - When replace_all=false: old_string must match exactly once (error if multiple)
  - When replace_all=true: replaces all occurrences
  - Preserves line ending style (CRLF/LF) of original file
  - Returns diff-like summary of changes
```

#### GlobTool
```
name: "Glob"
description: "Find files by glob pattern"
parameters:
  pattern: string      # Glob pattern (required, e.g. "src/**/*.ts")
  path?: string        # Search directory (default: workspace)
  include_dirs?: bool  # Include directories in results (default: true)
  
behavior:
  - Results sorted by modification time (most recent first)
  - Rejects pure wildcard patterns: "**", "**/*" (prevents context exhaustion)
  - Rejects brace expansion: "{a,b,c}" (glob engine treats braces as literals)
  - Maximum 1000 results
  - Symlink cycle detection via (dev, ino) tracking
  - Returns relative paths for display, absolute paths for downstream tools
  - Uses Kaos glob implementation
```

#### GrepTool
```
name: "Grep"
description: "Search file contents using ripgrep"
parameters:
  pattern: string                    # Regex pattern (required)
  path?: string                      # Search directory
  glob?: string                      # File filter: "*.ts", "*.{js,ts}"
  type?: string                      # File type: "ts", "py", "rust"
  output_mode?: "content" | "files_with_matches" | "count"
  -A?: int                           # Lines after match
  -B?: int                           # Lines before match
  -C?: int                           # Context lines (before + after)
  -i?: bool                          # Case insensitive
  -n?: bool                          # Show line numbers (default: true)
  multiline?: bool                   # Cross-line matching
  include_ignored?: bool             # Include gitignored files
  offset?: int                       # Skip first N results
  head_limit?: int                   # Max results (default: 250, 0 = unlimited)
  
behavior:
  - Uses ripgrep via Kaos exec (20-second timeout)
  - Modes: "content" (matching lines), "files_with_matches" (file paths), "count" (match counts)
  - Sensitive file filtering: blocks .env, SSH keys, credentials, AWS/GCP configs
  - Exemptions: .env.example, .env.sample, .env.template, public keys (.pub)
  - content mode: supports -A/-B/-C context lines
  - files_with_matches mode: sorted by modification time descending
  - Two-phase kill: SIGTERM → 5s → SIGKILL on timeout
```

#### ReadMediaFileTool
```
name: "ReadMediaFile"
description: "Read image or video files for multimodal LLM"
parameters:
  path: string        # File path (required)
  
behavior:
  - Maximum 100MB file size
  - Returns structured content with system summary (mime type, dimensions)
  - Requires model capability: image_in or video_in
  - Not available if model doesn't support vision
```

### Shell Tools

**Source**: `packages/agent-core/src/tools/builtin/shell/`

#### BashTool
```
name: "Bash"
description: "Execute shell commands"
parameters:
  command: string              # Shell command (required)
  cwd?: string                 # Working directory
  timeout?: int                # Milliseconds (default: 60000, max: 300000)
  description?: string         # Description for background tasks
  run_in_background?: bool     # Run asynchronously (default: false)
  disable_timeout?: bool       # No timeout for background tasks (default: false)
  dangerously_disable_sandbox?: bool  # Experimental

behavior:
  - Foreground: default 60s timeout, max 5 minutes
  - Background: default 10 min timeout, max 24h
  - Two-phase kill: SIGTERM → 5s grace → SIGKILL
  - Windows: uses configured Git Bash path, taskkill /T for tree kill
  - Stdin always closed (interactive commands get EOF immediately)
  - Output truncation: max 50,000 chars, line max 2,000 chars
  - Uses Kaos exec implementation
  - Background tasks return task ID immediately
```

### Web Tools

**Source**: `packages/agent-core/src/tools/builtin/web/`

#### FetchURLTool
```
name: "FetchURL"
description: "Fetch content from a URL"
parameters:
  url: string           # URL to fetch (required)
  
behavior:
  - Uses configured URL fetcher provider (local or Moonshot)
  - Local provider: SSRF protection (blocks private IPs, loopback, link-local)
  - Max 10MB response body
  - HTML extraction via Readability algorithm
  - Returns content with kind: "passthrough" or "extracted"
```

#### WebSearchTool
```
name: "WebSearch"
description: "Search the web"
parameters:
  query: string              # Search query (required)
  limit?: int                # Results count 1-20 (default: 5)
  include_content?: bool     # Include page content (default: false, expensive)
  
behavior:
  - Uses configured web search provider (Moonshot search service)
  - Requires backend search implementation
  - Not available if no search provider is configured
```

### Collaboration Tools

**Source**: `packages/agent-core/src/tools/builtin/collaboration/`

#### AgentTool
```
name: "Agent"
description: "Spawn a subagent for focused work"
parameters:
  task: string               # Task description (required)
  profile?: "coder" | "explore" | "plan"  # Default: "coder"
  mode?: "foreground" | "background"       # Default: "foreground"
  timeout?: int               # Milliseconds

behavior:
  - Foreground: blocks until subagent completes
  - Background: returns task_id immediately, agent runs in background
  - Subagent gets inherited model + thinking config from parent
  - Max recursion depth: 3 (subagent cannot spawn further subagents via Skill)
  - Explore profile: read-only tools only
  - Plan profile: restricted tools (no shell)
```

#### AskUserQuestionTool
```
name: "AskUserQuestion"
description: "Ask the user a structured question"
parameters:
  questions: [{
    question: string         # Question text
    header: string           # Short category label
    options: [{              # 2-4 options
      label: string
      description: string
    }]
    multiSelect?: bool       # Allow multiple selections
  }]  // 1-4 questions

behavior:
  - Sends structured question to TUI via reverse-RPC
  - Waits for user response
  - Returns JSON with answers
```

#### SkillTool
```
name: "Skill"
description: "Invoke a registered skill"
parameters:
  skill: string       # Skill name
  args: string        # Arguments (passed to skill template)
  
behavior:
  - Looks up skill in SkillRegistry
  - Renders skill prompt with args
  - Appends skill prompt as user message
  - Max recursion depth: 3 (MAX_SKILL_QUERY_DEPTH)
```

### Planning Tools

**Source**: `packages/agent-core/src/tools/builtin/planning/`

#### EnterPlanModeTool / ExitPlanModeTool
```
EnterPlanMode:
  name: "EnterPlanMode"
  description: "Enter plan mode to research before acting"
  parameters: {}  // No parameters

ExitPlanMode:
  name: "ExitPlanMode"
  description: "Exit plan mode and start implementing"
  parameters: {}  // No parameters
```

### Background Task Tools

**Source**: `packages/agent-core/src/tools/background/`

#### TaskCreateTool, TaskListTool, TaskOutputTool, TaskStopTool

```
TaskCreate:
  name: "TaskCreate"
  description: "Create a tracked task"
  parameters: { subject, description, activeForm }

TaskList:
  name: "TaskList"
  description: "List all pending/completed tasks"
  parameters: {}  // No parameters

TaskOutput:
  name: "TaskOutput"
  description: "Get output from a background task"
  parameters: { taskId, block?: bool, timeout?: int }

TaskStop:
  name: "TaskStop"
  description: "Stop a running task"
  parameters: { taskId }
```

## Tool Display Schemas

**Source**: `packages/agent-core/src/tools/display/schemas.ts`

Zod schemas that describe how tools should be rendered in the TUI:

```typescript
// Input display types — how the tool call looks in the UI
type ToolInputDisplay =
  | { type: 'command'; command: string }           // Bash
  | { type: 'file_io'; path: string; mode: string } // Write, Edit
  | { type: 'diff'; oldText: string; newText: string }  // Edit
  | { type: 'search'; pattern: string; path?: string }  // Grep, Glob
  | { type: 'url_fetch'; url: string }              // FetchURL
  | { type: 'agent_call'; task: string; profile: string } // Agent
  | { type: 'skill_call'; skill: string; args: string }   // Skill
  | { type: 'background_task'; description: string }      // TaskCreate
  | { type: 'task_stop'; taskId: string }                 // TaskStop
  | { type: 'plan_review'; planContent: string }
  | { type: 'generic'; title: string; details: string }

// Result display types
type ToolResultDisplay =
  | { type: 'command_output'; stdout: string; stderr: string; exitCode: number }
  | { type: 'file_content'; content: string; path: string; lines: number }
  | { type: 'diff'; patch: string }
  | { type: 'search_results'; results: SearchMatch[] }
  | { type: 'url_content'; url: string; content: string }
  | { type: 'agent_summary'; summary: string; agentId: string }
  | { type: 'background_task'; taskId: string; status: string }
  | { type: 'structured'; value: unknown }
  | { type: 'text'; text: string }
  | { type: 'error'; message: string }
  | { type: 'generic'; title: string; details: string }
```

## Path Access Security

**Source**: `packages/agent-core/src/tools/policies/path-access.ts`

```typescript
function resolvePathAccessPath(
  path: string,
  workspaceDirs: string[],         // [primaryWorkspaceDir, ...additionalDirs]
  mode: 'strict' | 'absolute-outside-allowed' | 'disabled',
): { allowed: boolean; canonicalPath: string; reason?: string };
```

Three guard modes:
- `strict`: Only paths within workspace directories are allowed
- `absolute-outside-allowed`: Absolute paths outside workspace are allowed
- `disabled`: No path restriction (use with caution)

Uses lexical canonicalization (not realpath, to avoid symlink following). The `isWithinDirectory()` function prevents shared-prefix attacks by checking that the path is genuinely within the directory (not just a matching prefix).

## Default Tool Permissions

**Source**: `packages/agent-core/src/tools/policies/default-permissions.ts`

| Tool | Default Permission |
|------|-------------------|
| Read, Glob, Grep, ReadMediaFile | Auto-allow (read-only) |
| WebSearch, FetchURL | Auto-allow (read-only) |
| Agent, AskUserQuestion, Skill | Auto-allow (safe) |
| EnterPlanMode, ExitPlanMode | Auto-allow (safe) |
| TodoList, TaskList, TaskOutput | Auto-allow (read-only) |
| Think | Auto-allow (no-op) |
| Write, Edit | Ask (write to filesystem) |
| Bash | Ask (execute commands) |
| TaskStop | Ask (manage tasks) |

## Re-implementation Notes

1. **Two-phase execution is the key pattern**: `resolveExecution()` is pure validation — it returns a `ToolExecution` with metadata. `execute()` is the actual side effect. This separation allows the permission system to inspect what a tool will do before it does it.

2. **JSON Schema for parameters**: Each tool declares its parameters as JSON Schema. The LLM receives these schemas and generates structured argument objects. Your tool definitions must include accurate JSON Schema — the LLM uses them to format calls correctly.

3. **Kaos is the I/O backbone**: Every tool that touches filesystem or processes does so through Kaos. If you're porting tools, you need Kaos first.

4. **Tool output truncation**: The `ToolResultBuilder` caps output at 50,000 characters and individual lines at 2,000 characters. Without truncation, a single tool call could overwhelm the context window.

5. **Background tasks run in the same process**: Background bash commands are spawned as child processes but tracked by the agent's `BackgroundManager`. If the agent process dies, background tasks are "lost" — the `reconcile()` method detects this on session resume.