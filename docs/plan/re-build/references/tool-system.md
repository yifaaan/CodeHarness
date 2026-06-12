# Tool System

Built-in tools and tool management.

## One-Liner

Defines everything the agent can do — reading/writing files, executing shell commands, searching the web, spawning subagents, and more.

## ExecutableTool Interface

```typescript
interface ExecutableTool<Input = unknown> {
  readonly name: string;
  readonly description: string;
  readonly parameters: JSONSchema;

  // Phase 1: Pure validation
  resolveExecution(args: Input, context: ToolExecutionMetadata): ToolExecution;
}

interface ToolExecution {
  readonly description: string;
  readonly accesses?: ToolAccesses;
  
  // Phase 2: Side-effectful execution
  execute(context: ExecutableToolContext): Promise<ToolResult>;
}
```

## Built-in Tools

| Category | Tools |
|----------|-------|
| **File** | Read, Write, Edit, Glob, Grep, ReadMediaFile |
| **Shell** | Bash |
| **Web** | FetchURL, WebSearch |
| **Collaboration** | Agent, AskUserQuestion, Skill |
| **Planning** | EnterPlanMode, ExitPlanMode |
| **Background** | TaskCreate, TaskList, TaskOutput, TaskStop |

## Key Tools

### Read
```yaml
name: Read
description: Read file contents with line numbers
parameters:
  path: string
  line_offset?: int  # 1-based, negative = from end
  n_lines?: int      # Max lines (default: all)
```
- Max 1000 lines or 100KB per call
- Detects binary files (rejects)

### Write
```yaml
name: Write
description: Create or overwrite a file
parameters:
  path: string
  content: string
  mode?: "overwrite" | "append"
```

### Edit
```yaml
name: Edit
description: Replace exact text in a file
parameters:
  file_path: string
  old_string: string
  new_string: string
  replace_all?: boolean
```
- Exact string replacement (not regex)
- Preserves line ending style

### Bash
```yaml
name: Bash
description: Execute shell commands
parameters:
  command: string
  cwd?: string
  timeout?: int           # Default 60000ms, max 300000ms
  run_in_background?: bool
```
- Two-phase kill: SIGTERM → 5s → SIGKILL
- Output truncation: max 50,000 chars

### Agent
```yaml
name: Agent
description: Spawn a subagent for focused work
parameters:
  task: string
  profile?: "coder" | "explore" | "plan"
  mode?: "foreground" | "background"
  timeout?: int
```
- Max recursion depth: 3
- Explore profile: read-only tools
- Plan profile: restricted tools

## ToolManager

```typescript
class ToolManager {
  setActiveTools(names: string[]): void;
  registerUserTool(definition: UserToolDefinition): void;
  registerMcpServer(serverName: string, tools: MCPToolDefinition[]): void;
  get loopTools(): ExecutableTool[];
}
```

## Tool Accesses

```typescript
interface ToolAccesses {
  readFiles?: boolean;
  writeFiles?: boolean;
  network?: boolean;
  executeCommands?: boolean;
  manageAgents?: boolean;
  manageTasks?: boolean;
}
```

## Default Permissions

| Tool | Default |
|------|---------|
| Read, Glob, Grep, ReadMediaFile | Auto-allow |
| WebSearch, FetchURL | Auto-allow |
| Agent, AskUserQuestion, Skill | Auto-allow |
| EnterPlanMode, ExitPlanMode | Auto-allow |
| TodoList, TaskList, TaskOutput | Auto-allow |
| **Write, Edit** | **Ask** |
| **Bash** | **Ask** |
| **TaskStop** | **Ask** |

## Path Access Security

Three guard modes:
- `strict`: Only paths within workspace directories
- `absolute-outside-allowed`: Absolute paths outside workspace allowed
- `disabled`: No path restriction

## See Also

- [kaos-interface.md](kaos-interface.md) — I/O abstraction
- [permission-hooks.md](permission-hooks.md) — Permission system
