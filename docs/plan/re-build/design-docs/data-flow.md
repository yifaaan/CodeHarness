# Data Flow

Detailed request/response flow diagrams.

## User Prompt Flow

```
User types prompt in TUI
    │
    ▼
┌─────────────┐
│ TurnFlow    │
│ .prompt()   │
└──────┬──────┘
       │
       ▼
┌────────────────────────────────────────────────────┐
│ turnWorker():                                      │
│  1. Log turn.prompt record                         │
│  2. Create AbortController                         │
│  3. Trigger UserPromptSubmit hook                  │
│  4. Append user message to context               │
│  5. Call runTurn()                                 │
└────────────────────────────────────────────────────┘
       │
       ▼
┌────────────────────────────────────────────────────┐
│ runTurn():                                         │
│                                                    │
│  step = 0                                          │
│  while step < maxSteps:                            │
│    step++                                          │
│                                                    │
│    beforeStep hook:                                │
│      - Flush steer buffer                          │
│      - Check compaction                            │
│      - Inject dynamic content                      │
│      - Fire PreToolUse hook                        │
│                                                    │
│    executeLoopStep():                              │
│      - Emit step.started                           │
│      - Call LLM.chat()                             │
│      - Stream response (assistant.delta)           │
│      - If tool calls: runToolCallBatch()           │
│      - Emit step.completed                         │
│                                                    │
│    afterStep hook                                  │
│                                                    │
│    Check stopReason:                               │
│      - end_turn → exit                             │
│      - tool_use → continue loop                    │
│      - max_tokens → check shouldContinue           │
└────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────┐
│ turnWorker  │
│ completes   │
└──────┬──────┘
       │
       ▼
Emit turn.ended event → TUI renders result
```

## Tool Execution Flow

```
LLM returns tool calls
    │
    ▼
┌────────────────────────────────────────────────────┐
│ runToolCallBatch():                                │
│                                                    │
│  for each toolCall:                                │
│    1. Find matching tool                           │
│    2. prepareToolExecution hook                    │
│       └── PermissionManager.beforeToolCall()       │
│           ├── yolo → auto-allow                    │
│           ├── auto → check rules                     │
│           └── manual → requestApproval             │
│    3. Check deduplication                            │
│    4. Emit tool.call.started                         │
│    5. Execute tool                                   │
│    6. Emit tool.result                               │
│    7. finalizeToolResult hook                        │
└────────────────────────────────────────────────────┘
```

## Event Flow

```
Agent emits events via dispatchEvent()
    │
    ├── recorded(event) ──> AgentRecords ──> wire.jsonl
    │
    └── live(event) ──> RPC ──> SDK ──> TUI.render()

Event types:
- turn.started, turn.ended
- turn.step.started, turn.step.completed
- assistant.delta, thinking.delta
- tool.call.started, tool.result, tool.progress
- error, agent.status.updated
- subagent.spawned, subagent.completed, subagent.failed
- compaction.started, compaction.completed
- background.started, background.completed
```

## Session Resume Flow

```
Session.resume(sessionId)
    │
    ▼
┌────────────────────────────────────────────────────┐
│ 1. Load session state from state.json                │
│ 2. Initialize SkillRegistry                          │
│ 3. Initialize McpConnectionManager                   │
│ 4. Create main Agent                                 │
│ 5. Agent.resume():                                   │
│      a. Set restoring = true                         │
│      b. records.replay():                          │
│         - Read wire.jsonl                            │
│         - For each: restoreAgentRecord(agent, rec) │
│      c. background.reconcile()                       │
│      d. turn.finishResume()                          │
│      e. Set restoring = false                        │
│ 6. Fire SessionStart hook                            │
└────────────────────────────────────────────────────┘
```

## Configuration Flow

```
TOML file (~/.kimi-code/config.toml)
    │
    ▼
KimiConfig (zod-validated schema)
    │
    ├──> ProviderManager.resolveProviderForModel()
    │       ├──> Lookup model alias in [models] table
    │       ├──> Resolve provider config from [providers] table
    │       ├──> Resolve OAuth token if configured
    │       └──> Return RuntimeProvider
    │
    ├──> PermissionConfig → PermissionManager
    ├──> HookConfig → HookEngine
    ├──> MCPConfig → McpConnectionManager
    └──> BackgroundConfig → BackgroundManager
```

## See Also

- [system-overview.md](system-overview.md) — System boundaries
- [../ARCHITECTURE.md](../ARCHITECTURE.md) — Architecture overview
