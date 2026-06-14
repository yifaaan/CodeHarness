# Permission and Hooks System

**Source**: `packages/agent-core/src/agent/permission/`, `packages/agent-core/src/agent/hooks/`

## Purpose

The **Permission System** controls what tools the agent can execute and under what conditions. It's a rule engine that evaluates each tool call against configured rules, policies, and the current permission mode.

The **Hooks System** allows external commands to intercept agent lifecycle events — before tool execution, after tool results, on session start/end, and more. Hooks run as local subprocesses and can block or allow operations.

## Permission Architecture

```
Tool call requested (by LLM)
    │
    ▼
PermissionManager.beforeToolCall(toolCall)
    │
    ├── Step 1: Check permission mode
    │   ├── yolo → auto-allow (skip all checks)
    │   ├── auto → allow certain tools automatically
    │   └── manual → check rules and policies
    │
    ├── Step 2: Check user-defined rules
    │   ├── Find matching rule by pattern
    │   ├── allow → proceed
    │   ├── deny → block with reason
    │   └── ask → request approval
    │
    ├── Step 3: Check built-in policies
    │   ├── Policy evaluates the action
    │   └── allow/deny/ask result
    │
    ├── Step 4: If unresolved → request approval from user
    │   ├── RPC.requestApproval(toolCallInfo)
    │   ├── User approves/denies in TUI
    │   └── Record approval result
    │
    └── Return: allow | deny
```

## PermissionManager

**Source**: `packages/agent-core/src/agent/permission/index.ts`

```typescript
class PermissionManager {
  constructor(parent: Session | Agent, config: PermissionConfig);

  /** Current permission mode */
  mode: PermissionMode;  // 'manual' | 'auto' | 'yolo'

  /** Check if a tool call is allowed */
  async beforeToolCall(toolCall: ToolCallInfo): Promise<PermissionResult>;

  /** Record an approval result (from user or auto) */
  recordApprovalResult(result: ApprovalResult): void;

  /** Get effective rules (inherits from parent for subagents) */
  effectiveRules(): PermissionRule[];

  /** Set permission mode */
  setMode(mode: PermissionMode): void;

  /** Get current mode */
  getMode(): PermissionMode;
}

type PermissionMode = 'manual' | 'auto' | 'yolo';

interface PermissionResult {
  decision: 'allow' | 'deny';
  reason?: string;
  blocked?: boolean;      // true if a deny rule blocked this call
  policy?: string;        // Name of the policy that made the decision
}

interface ToolCallInfo {
  name: string;
  args: Record<string, unknown>;
  agentType: 'main' | 'sub';
  accesses?: ToolAccesses;  // From tool.resolveExecution()
}

interface ApprovalResult {
  toolCallId: string;
  decision: 'allow' | 'deny';
  scope?: 'turn-override' | 'session-runtime' | 'project' | 'user';
}
```

## Permission Rules

Rules are defined in `config.toml` and can also be created dynamically from user approvals:

```typescript
interface PermissionRule {
  decision: 'allow' | 'deny' | 'ask';
  scope: 'turn-override' | 'session-runtime' | 'project' | 'user';
  pattern: string;           // DSL: "Read(/etc/**)", "Bash(rm *)"
  reason?: string;           // Displayed when the rule matches
  expiryMs?: number;         // Optional expiry (for temporary rules)
}
```

### Pattern DSL

```
Format: <ToolName>(<glob-pattern>)

Examples:
  "Read(/etc/**)"              — Allow Read on /etc/*
  "Bash(rm *)"                 — Deny rm commands
  "Write(/etc/**)"             — Ask before writing to /etc
  "Edit(src/**)"               — Allow edits in src/ directory
  "*"                          — Match all tools
```

### Pattern Matching

Rules are evaluated in order. The first matching rule wins:

```typescript
function matchRule(toolName: string, args: Record<string, unknown>, rules: PermissionRule[]): PermissionRule | null {
  for (const rule of rules) {
    if (matchesPattern(rule.pattern, toolName, args)) {
      return rule;
    }
  }
  return null;  // No matching rule → ask user (in manual mode)
}
```

### Rule Scopes

| Scope | Duration | Source |
|-------|----------|--------|
| `turn-override` | Current turn only | User approval dialog "just this once" |
| `session-runtime` | Session lifetime | User approval dialog "always in this session" |
| `project` | Persistent | Pre-configured in config.toml |
| `user` | Persistent | Pre-configured in config.toml |

## Permission Policies

**Source**: `packages/agent-core/src/agent/permission/policies/`

Policies are programmatic permission checks that extend beyond pattern matching:

```typescript
interface PermissionPolicy {
  name: string;
  evaluate(action: ToolCallInfo, agent: Agent): Promise<PermissionPolicyResult | null>;
}

interface PermissionPolicyResult {
  decision: 'allow' | 'deny' | 'ask';
  reason?: string;
  policy: string;
}
```

Built-in policies:

| Policy | Purpose |
|--------|---------|
| `default-git-cwd-write.ts` | Deny Write/Edit to files outside the git working directory |
| `yolo-workspace-access.ts` | In YOLO mode, only auto-allow tools accessing workspace files |
| `plan.ts` | In plan mode, restrict certain tools |
| `ask-user-question.ts` | Always allow AskUserQuestion (it's user-facing) |

## Approval Flow (TUI Integration)

```
When permission is unresolved (ask):
  1. PermissionManager calls agent's RPC:
     rpc.requestApproval({
       toolCallId,
       toolName,
       args,
       description,     // From tool.resolveExecution()
       accesses,        // What the tool will access
     })
  
  2. TUI renders approval dialog:
     ┌─────────────────────────────────────────┐
     │  Tool: Write                            │
     │  Path: /etc/config.json                 │
     │                                          │
     │  [Allow once] [Allow always] [Deny]     │
     └─────────────────────────────────────────┘
  
  3. User selects an option:
     - "Allow once" → scope = turn-override
     - "Allow always" → scope = session-runtime
     - "Deny" → scope = turn-override, decision = deny
  
  4. PermissionManager records the result:
     recordApprovalResult({ toolCallId, decision, scope })
     
     If scope is session-runtime:
       → Auto-generate a PermissionRule and add it to rules
```

## HookEngine

**Source**: `packages/agent-core/src/agent/hooks/engine.ts`

```typescript
class HookEngine {
  constructor(hooks: HookDef[]);

  /** Fire hooks (best-effort — failures don't block) */
  trigger(event: HookEvent, data: HookData): Promise<HookResult[]>;

  /** Fire hooks and return first blocking result */
  triggerBlock(event: HookEvent, data: HookData): Promise<HookResult | null>;

  /** Fire hooks without waiting for completion */
  fireAndForgetTrigger(event: HookEvent, data: HookData): void;
}

interface HookDef {
  event: string;       // Event type
  matcher?: string;    // Regex to match against event target
  command: string;     // Shell command to execute
  timeout?: number;    // Seconds (1-600, default 30)
}

interface HookData {
  // Event-specific data, serialized to JSON and piped to command's stdin
  [key: string]: unknown;
}

interface HookResult {
  action: 'allow' | 'block';
  reason?: string;
  message?: string;
  stdout?: string;
  stderr?: string;
  exitCode?: number | null;
}
```

### Hook Execution

When a hook triggers:

1. The command is spawned as a subprocess
2. Event payload is written as JSON to the command's stdin
3. Command's stdout/stderr are captured
4. Exit code determines success/failure
5. For `triggerBlock`, the `action` field determines whether to block

**Fail-open policy**: If a hook command fails (non-zero exit, timeout, or crash), the hook is treated as "allow" by default. This prevents a broken script from blocking the agent.

```
Hook command receives on stdin:
{
  "event": "PreToolUse",
  "toolCall": {
    "name": "Bash",
    "args": { "command": "rm -rf /" },
    "toolCallId": "..."
  },
  "agent": { "type": "main" },
  "session": { "id": "...", "title": "..." }
}

Hook command returns:
{"action": "deny", "reason": "Dangerous command blocked"}
```

### Hook Events

| Event | Trigger | triggerBlock? | Payload |
|-------|---------|---------------|---------|
| `PreToolUse` | Before tool execution | Yes | toolCall, agent, session |
| `PostToolUse` | After tool result | No | toolCall, result, agent, session |
| `PostToolUseFailure` | After tool error | No | toolCall, error, agent, session |
| `UserPromptSubmit` | Before user prompt is sent to agent | Yes | input, origin, agent, session |
| `Stop` | Agent stop | No | agent, session |
| `StopFailure` | Agent stop failure | No | error, agent, session |
| `SessionStart` | Session created/resumed | No | session |
| `SessionEnd` | Session closing | No | session |
| `SubagentStart` | Subagent spawned | No | task, parentTurnId |
| `SubagentStop` | Subagent completed | No | summary, agentId |
| `PreCompact` | Before context compaction | No | tokenCount, contextSize |
| `PostCompact` | After context compaction | No | newTokenCount |
| `Notification` | General notification | No | message, type |

### Hook Configuration in config.toml

```toml
[[hooks]]
event = "PreToolUse"
matcher = "Bash"                    # Only match Bash tool calls
command = "node ~/.kimi-code/hooks/check-bash.mjs"
timeout = 5

[[hooks]]
event = "Notification"
matcher = "task\\.completed"        # Regex: match "task.completed"
command = "terminal-notifier -title Kimi -message 'Task done'"

[[hooks]]
event = "UserPromptSubmit"
command = "node ~/.kimi-code/hooks/audit-log.mjs"  # No matcher = all prompts
```

### Hook Lifecycle

```
Agent turn starts
  │
  ├── UserPromptSubmit hook (can modify/block input)
  │
  ├── Step loop:
  │   ├── PreToolUse hook (can block tool)
  │   ├── Tool executes
  │   ├── PostToolUse hook (notification)
  │   └── PostToolUseFailure hook (on error)
  │
  ├── PreCompact hook (before compaction)
  ├── PostCompact hook (after compaction)
  │
  └── SessionEnd hook (on session close)
```

## Re-implementation Notes

1. **Permission is a rule engine**: Three inputs → mode, rules, policies → one output (allow/deny). The evaluation order is: mode check → rule match → policy evaluation → user approval. First decision wins.

2. **Pattern DSL is simple but limited**: `<ToolName>(<glob>)`. The glob matches against stringified arguments. For a reimplementation, start with exact tool name matching and add glob support later.

3. **Hooks are subprocess-based**: Each hook spawns a child process and pipes JSON to stdin. The fail-open policy (hook failure = allow) prevents broken scripts from blocking the agent. For security-critical hooks, use the return value.

4. **Subagent inherits parent permissions**: Subagents use the same `PermissionManager` instance (or a copy of the rules). This means permissions set by the user for the main agent also apply to subagents.

5. **Session-scoped approvals auto-create rules**: When a user clicks "Allow always", a new `PermissionRule` with scope `session-runtime` is created. These rules persist for the session but not across sessions.

6. **trigger vs triggerBlock**: Use `trigger` for informational hooks (notifications, logging). Use `triggerBlock` for security hooks that can block operations. Only `PreToolUse` and `UserPromptSubmit` support blocking.