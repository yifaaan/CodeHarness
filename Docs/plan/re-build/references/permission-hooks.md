# Permission and Hooks

Permission system and event hooks.

## One-Liner

Permission system controls tool execution via rules and policies. Hooks allow external commands to intercept agent lifecycle events.

## Permission Architecture

```
Tool call requested
    │
    ▼
PermissionManager.beforeToolCall()
    │
    ├── Step 1: Check mode
    │   ├── yolo → auto-allow
    │   ├── auto → check rules
    │   └── manual → check rules
    │
    ├── Step 2: Check rules
    │   ├── allow → proceed
    │   ├── deny → block
    │   └── ask → request approval
    │
    ├── Step 3: Check policies
    │   └── Policy evaluation
    │
    └── Step 4: If unresolved → request approval from user
```

## PermissionManager

```typescript
class PermissionManager {
  mode: PermissionMode;  // 'manual' | 'auto' | 'yolo'
  
  async beforeToolCall(toolCall: ToolCallInfo): Promise<PermissionResult>;
  recordApprovalResult(result: ApprovalResult): void;
  setMode(mode: PermissionMode): void;
}

interface PermissionResult {
  decision: 'allow' | 'deny';
  reason?: string;
  blocked?: boolean;
  policy?: string;
}
```

## Permission Rules

```typescript
interface PermissionRule {
  decision: 'allow' | 'deny' | 'ask';
  scope: 'turn-override' | 'session-runtime' | 'project' | 'user';
  pattern: string;  // "Read(/etc/**)", "Bash(rm *)"
  reason?: string;
  expiryMs?: number;
}
```

### Pattern DSL

```
Format: <ToolName>(<glob-pattern>)

Examples:
  "Read(/etc/**)"     — Allow Read on /etc/*
  "Bash(rm *)"        — Deny rm commands
  "Write(/etc/**)"    — Ask before writing
  "*"                 — Match all tools
```

### Rule Scopes

| Scope | Duration | Source |
|-------|----------|--------|
| `turn-override` | Current turn only | "Allow once" |
| `session-runtime` | Session lifetime | "Allow always" |
| `project` | Persistent | config.toml |
| `user` | Persistent | config.toml |

## Permission Policies

Programmatic checks beyond pattern matching:

| Policy | Purpose |
|--------|---------|
| `default-git-cwd-write` | Deny Write outside git working directory |
| `yolo-workspace-access` | In YOLO mode, only auto-allow workspace files |
| `plan` | In plan mode, restrict certain tools |
| `ask-user-question` | Always allow AskUserQuestion |

## HookEngine

```typescript
class HookEngine {
  trigger(event: HookEvent, data: HookData): Promise<HookResult[]>;
  triggerBlock(event: HookEvent, data: HookData): Promise<HookResult | null>;
  fireAndForgetTrigger(event: HookEvent, data: HookData): void;
}

interface HookDef {
  event: string;       // Event type
  matcher?: string;    // Regex to match target
  command: string;     // Shell command to execute
  timeout?: number;    // Seconds (1-600, default 30)
}

interface HookResult {
  action: 'allow' | 'block';
  reason?: string;
}
```

## Hook Events

| Event | Trigger | Can Block |
|-------|---------|-----------|
| `PreToolUse` | Before tool execution | Yes |
| `PostToolUse` | After tool result | No |
| `UserPromptSubmit` | Before user prompt | Yes |
| `SessionStart` | Session created | No |
| `SessionEnd` | Session closing | No |
| `PreCompact` | Before compaction | No |
| `PostCompact` | After compaction | No |

## Fail-Open Policy

If a hook command fails (non-zero exit, timeout, crash), it's treated as "allow" by default. This prevents broken scripts from blocking the agent.

## See Also

- [config-schema.md](config-schema.md) — Permission configuration
- [tool-system.md](tool-system.md) — Tool execution
