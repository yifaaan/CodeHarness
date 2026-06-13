# Agent Core Engine

**Source**: `packages/agent-core/src/agent/index.ts`

## Purpose

The `Agent` class is the **central orchestrator** of the entire system. It owns and coordinates all subsystems — context memory, tool management, permissions, turn flow, hooks, planning mode, background tasks, compaction, event recording, and skill activation.

The Agent is the "IOC container" of the kimi-code engine. Every subsystem is publicly accessible (readonly fields), receives a reference to its parent Agent, and can interact with other subsystems through the Agent.

## Agent Architecture

```
Agent
│
├── runtime: RuntimeConfig           — Environment (kaos, os, env vars)
├── records: AgentRecords            — Event-sourced persistence (wire.jsonl)
├── fullCompaction: FullCompaction   — Context compaction
├── context: ContextMemory           — Conversation history + token tracking
├── config: ConfigState              — Model, provider, system prompt, thinking
├── turn: TurnFlow                   — Turn lifecycle (prompt/steer/cancel)
├── injection: InjectionManager      — Dynamic context injection (plan + permission reminders)
├── permission: PermissionManager    — Permission rules + approval flow
├── planMode: PlanMode               — Planning mode state
├── usage: UsageRecorder             — Token usage tracking
├── tools: ToolManager               — Tool registration + active set management
├── background: BackgroundManager    — Background task lifecycle
├── replayBuilder: ReplayBuilder     — Record capture for session export
├── skills: SkillManager             — Skill activation
└── hooks: HookEngine                — Event hooks (inherited from Session)
```

## Construction Order (Load-Bearing)

The order in which subsystems are initialized matters because later subsystems reference earlier ones:

```
1. records ────> 2. fullCompaction ────> 3. context
       │                                      │
       └──────────────────────────────────────┘
                       │
                       ▼
    4. config ───> 5. turn ───> 6. injection ───> 7. permission
                                                       │
                                                       ▼
                             8. planMode ───> 9. usage ───> 10. tools
                                                               │
                                                               ▼
                                    11. background ───> 12. replayBuilder
```

**Why this order matters**:
- `records` must be first — every subsystem logs records, and records needs to be ready
- `fullCompaction` needs records to log compaction events
- `context` needs compaction for overflow handling
- `config` is independent but needed by turn flow
- `turn` needs context and config to run conversations
- `injection` needs turn to inject before steps
- `permission` is independent but referenced by tools
- `planMode`, `usage`, `tools` are independent subsystems
- `background` needs tools for background tasks
- `replayBuilder` needs everything to capture records

### Constructor Pseudocode

```
constructor(parent, config, runtime):
  this.runtime = runtime
  this.records = AgentRecords(config.records)
  this.fullCompaction = FullCompaction(this, config.compaction)
  this.context = ContextMemory(this, config.context)  // needs fullCompaction for overflow
  this.config = ConfigState(config.config)             // model + provider settings
  this.turn = TurnFlow(this, config.turn)              // needs context, config
  this.injection = InjectionManager(this)              // references turn for inject timing
  this.permission = PermissionManager(parent, config.permission)
  this.planMode = PlanMode(this, config.planMode)
  this.usage = UsageRecorder(config.usage)
  this.tools = ToolManager(this, config.tools)
  this.background = BackgroundManager(this, config.background)
  this.replayBuilder = ReplayBuilder()
  this.skills = SkillManager(this, config.skills)
  this.hooks = parent.hooks  // inherits hooks from Session
```

## Agent Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│  CREATION                                                        │
│                                                                  │
│  Agent.create(config, runtime)                                   │
│    ├── Initialize all subsystems in order                        │
│    ├── Apply agent profile (system prompt + default tools)       │
│    └── return Agent                                              │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│  RESUME (from persisted state)                                   │
│                                                                  │
│  agent.resume()                                                  │
│    ├── records.replay() — replay all wire.jsonl events           │
│    │     └── restoreAgentRecord() for each record                │
│    ├── background.reconcile() — check lost background tasks      │
│    └── turn.finishResume() — clear resuming state                │
└────────────────────────────────���────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│  ACTIVE CONVERSATION                                             │
│                                                                  │
│  Repeated loop:                                                  │
│    User input → turn.prompt()                                    │
│      └── turnWorker()                                            │
│            └── runTurn() (see 08-loop)                           │
│                  ├── beforeStep → inject → compact               │
│                  ├── executeLoopStep → LLM + tools               │
│                  └── afterStep                                   │
│    Events emitted to TUI via RPC                                 │
│    Records written to wire.jsonl                                 │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│  SHUTDOWN                                                        │
│                                                                  │
│  agent.close()                                                   │
│    ├── turn.cancel() — stop any active turn                     │
│    ├── background.cancelAll() — stop background tasks            │
│    ├── records.flush() — ensure wire.jsonl is written            │
│    └── (session persists state.json metadata)                    │
└─────────────────────────────────────────────────────────────────┘
```

## RPC Methods

The Agent exposes methods via the `rpcMethods` property, which forms the `AgentAPI` interface used by the SDK and TUI:

```typescript
interface AgentAPI {
  // --- Turn control ---
  prompt(input: ContentPart[], origin: PromptOrigin): Promise<TurnEndResult>;
  steer(input: ContentPart[], origin: PromptOrigin): Promise<void>;
  cancel(): void;
  waitForCurrentTurn(signal?: AbortSignal): Promise<TurnEndResult>;
  
  // --- Configuration ---
  setThinking(level: ThinkingEffort): void;
  setPermission(mode: PermissionMode): void;
  setModel(model: string): Promise<void>;
  
  // --- Plan mode ---
  enterPlan(): Promise<void>;
  cancelPlan(): void;
  clearPlan(): void;
  exitPlan(): void;
  
  // --- Context management ---
  compact(signal: AbortSignal, instruction?: string): Promise<void>;
  cancelCompaction(): void;
  clear(): void;
  
  // --- Tool management ---
  registerTool(tool: UserToolDefinition): void;
  unregisterTool(name: string): void;
  setActiveTools(tools: string[]): void;
  getActiveTools(): string[];
  
  // --- Background tasks ---
  stopBackground(taskId: string): void;
  waitBackgroundTask(taskId: string): Promise<void>;
  
  // --- State queries ---
  getConfig(): AgentConfigInfo;
  getContext(): ContextInfo;
  getTurnState(): TurnStateInfo;
  listBackgroundTasks(): BackgroundTaskInfo[];
}
```

## Agent Types

```typescript
type AgentType = 'main' | 'sub' | 'independent';
```

| Type | Created By | Purpose |
|------|-----------|---------|
| `main` | Session | Primary user-facing agent. Owns the conversation with the user. |
| `sub` | Agent tool (subagent spawn) | Temporary worker for focused subtasks. Can't talk to user directly. |
| `independent` | SDK/embedding | Operates outside a session. No UI attachment. |

## Agent Profile

```typescript
interface AgentProfile {
  name: string;
  systemPrompt?: string;     // Base system prompt for this agent type
  tools?: string[];          // Tool names to enable by default
  attributes?: AgentAttributes;
}

const DEFAULT_AGENT_PROFILES: Record<string, AgentProfile> = {
  coder: {
    name: 'Coder',
    systemPrompt: '...',     // General coding assistant
    tools: ['Read', 'Write', 'Edit', 'Glob', 'Grep', 'Bash', 'FetchURL', 'WebSearch', ...],
  },
  explore: {
    name: 'Explore',
    systemPrompt: '...',     // Read-only research assistant
    tools: ['Read', 'Glob', 'Grep', 'FetchURL', 'WebSearch', ...],  // No Write/Edit/Bash
  },
  plan: {
    name: 'Plan',
    systemPrompt: '...',     // Architecture & design assistant
    tools: ['Read', 'Glob', 'Grep'],  // Very restricted toolset
  },
};
```

## Event Emission

The Agent emits typed events for real-time communication with the UI:

```typescript
type AgentEvent =
  | { type: 'turn.started'; turnId: string }
  | { type: 'turn.ended'; turnId: string; result: TurnEndResult }
  | { type: 'turn.step.started'; step: number; turnId: string }
  | { type: 'turn.step.completed'; step: number; turnId: string }
  | { type: 'assistant.delta'; content: ContentPart }
  | { type: 'thinking.delta'; content: ThinkPart }
  | { type: 'tool.call.started'; toolCall: ToolCall }
  | { type: 'tool.result'; result: ToolResultDisplay }
  | { type: 'tool.progress'; toolCallId: string; update: ToolUpdate }
  | { type: 'tool.list.updated'; tools: string[] }
  | { type: 'error'; error: KimiErrorPayload }
  | { type: 'agent.status.updated'; status: AgentStatus }
  | { type: 'subagent.spawned'; agentId: string; profile: string }
  | { type: 'subagent.completed'; agentId: string; summary: string }
  | { type: 'subagent.failed'; agentId: string; error: string }
  | { type: 'compaction.started'; ... }
  | { type: 'compaction.completed'; ... }
  | { type: 'background.started'; taskId: string }
  | { type: 'background.completed'; taskId: string }
  | { type: 'skill.invoked'; skillName: string }
  | // ... more event types
```

The Agent emits events via:
- `emitEvent(event)` → direct event emission (used for ad-hoc events)
- `emitStatusUpdated()` → emits `agent.status.updated` with current status

Events are forwarded through the RPC channel to the SDK, which delivers them to the TUI for rendering.

## Data Flow Summary

```
User Input
    │
    ▼
TurnFlow.prompt()
    │
    ▼
TurnFlow.turnWorker()
    │
    ├── UserPromptSubmit hook (can modify/block input)
    │
    ▼
runTurn() (see 08-loop)
    │
    ├── Step loop:
    │   ├── 1. beforeStep hook → InjectionManager.inject() → FullCompaction.beforeStep()
    │   ├── 2. executeLoopStep:
    │   │       ├── LLM.chat(systemPrompt, tools, history) → llm provider
    │   │       ├── Text/thinking streaming → events → TUI
    │   │       ├── Tool calls → PermissionManager.beforeToolCall() → execute → context.append
    │   │       └── Record each event to wire.jsonl
    │   ├── 3. afterStep hook
    │   └── 4. Check stop reason → continue or end
    │
    ▼
TurnEndResult → event to TUI
```

## Re-implementation Notes

1. **Constructor order is critical**: The Agent constructor initializes subsystems in a specific sequence. Changing this order will break cross-references between subsystems. Follow the exact order: records → compaction → context → config → turn → injection → permission → planMode → usage → tools → background → replayBuilder.

2. **Agent is the composition root**: Every subsystem is `public readonly` and receives the parent Agent. This makes subsystems independently testable — you can construct an Agent with mock subsystems.

3. **Event emission is the UI contract**: The TUI renders purely from AgentEvents. If you change event types, you must update the TUI rendering code. The event types form a contract between the engine and the UI.

4. **Resume is distinct from create**: `resume()` replays all records to reconstruct state. It must NOT emit UI events, call LLM, or execute tools during replay. The `restoring` flag on AgentRecords gates this behavior.

5. **Subagent isolation**: Each subagent is a full Agent instance with its own context, records, and tools. It inherits permissions from the parent but has its own context window. Subagents cannot spawn further subagents (recursion limit = 1).
