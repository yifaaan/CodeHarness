# Agent Lifecycle

Agent, Session, RPC, and TurnFlow.

## One-Liner

The Agent is the central orchestrator that coordinates all subsystems. Session manages the agent lifecycle. TurnFlow bridges user input to the execution loop.

## Agent Architecture

```
Agent
├── runtime: RuntimeConfig
├── records: AgentRecords          # Event sourcing
├── fullCompaction: FullCompaction # Context compaction
├── context: ContextMemory         # Conversation history
├── config: ConfigState            # Model settings
├── turn: TurnFlow                 # Turn lifecycle
├── injection: InjectionManager    # Dynamic content
├── permission: PermissionManager # Permission rules
├── planMode: PlanMode             # Plan mode state
├── usage: UsageRecorder           # Token tracking
├── tools: ToolManager             # Tool registration
├── background: BackgroundManager  # Background tasks
├── replayBuilder: ReplayBuilder  # Session export
├── skills: SkillManager           # Skill activation
└── hooks: HookEngine              # Event hooks
```

## Construction Order (Load-Bearing)

```
1. records → 2. fullCompaction → 3. context
                    │
                    ▼
4. config → 5. turn → 6. injection → 7. permission
                                            │
                                            ▼
                    8. planMode → 9. usage → 10. tools
                                                │
                                                ▼
                            11. background → 12. replayBuilder
```

## Agent Types

| Type | Purpose | Can Spawn Subagents |
|------|---------|---------------------|
| `main` | Primary user-facing agent | Yes |
| `sub` | Temporary worker for focused tasks | No |
| `independent` | Operates outside session | No |

## Session Lifecycle

```
Session.create(config)
    ├── Create session directory
    ├── Initialize SkillRegistry
    ├── Initialize McpConnectionManager
    ├── Create main Agent
    └── Fire SessionStart hook

Session.resume(sessionId)
    ├── Load state from state.json
    ├── Initialize subsystems
    ├── Create Agent
    ├── Agent.resume() — replay wire.jsonl
    └── Fire SessionStart hook
```

## TurnFlow Interface

```typescript
class TurnFlow {
  prompt(input: ContentPart[], origin: PromptOrigin): Promise<TurnEndResult>;
  steer(input: ContentPart[], origin: PromptOrigin): Promise<void>;
  cancel(turnId?: string): void;
  waitForCurrentTurn(signal?: AbortSignal): Promise<TurnEndResult>;
}
```

- `prompt`: Start new turn (rejects if one active)
- `steer`: Buffer input during active turn
- Steer buffer flushed at `beforeStep` hook

## RPC Protocol

Bidirectional method exchange:
```typescript
function createRPC<Left, Right>(): [RPCClient<Left, Right>, RPCClient<Right, Left>];
```

- Side A calls methods on Side B via `rpc.methodName(args)`
- Side B calls methods on Side A via `reverseRpc.methodName(args)`
- Automatic serialization/deserialization

## AgentAPI (RPC Methods)

| Category | Methods |
|----------|---------|
| Turn | `prompt()`, `steer()`, `cancel()`, `waitForCurrentTurn()` |
| Config | `setThinking()`, `setPermission()`, `setModel()` |
| Plan | `enterPlan()`, `cancelPlan()`, `clearPlan()`, `exitPlan()` |
| Context | `compact()`, `cancelCompaction()`, `clear()` |
| Tools | `registerTool()`, `unregisterTool()`, `setActiveTools()` |
| Background | `stopBackground()`, `waitBackgroundTask()` |

## Subagent Spawn Flow

```
1. resolveProfile("explore")
2. Create child Agent with inherited config
3. Run task: agent.turn.prompt(task, { origin: 'subagent_spawn' })
4. Check summary length, request continuation if needed
5. Return { summary, agentId, terminated }
```

## See Also

- [loop-engine.md](loop-engine.md) — Turn execution
- [context-compaction.md](context-compaction.md) — Context memory
- [records-replay.md](records-replay.md) — Event sourcing
