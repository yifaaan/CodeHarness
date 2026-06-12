# Session and RPC Layer

**Source**: `packages/agent-core/src/session/`, `packages/agent-core/src/rpc/`

## Purpose

The **Session** class is the top-level manager that owns the entire agent lifecycle — creating agents, managing MCP connections, discovering skills, and persisting state.

The **RPC layer** provides bidirectional method exchange between the agent engine and the SDK/CLI. It defines the complete IPC protocol: method calls, event streams, and error handling.

## Session Architecture

```
Session
│
├── agents: Map<string, Agent>     — All agents in this session
│   ├── 'main'                        — Primary user-facing agent
│   └── <subagentId>                  — Spawned subagents
│
├── skills: SkillRegistry              — Skill discovery and lookup
├── mcp: McpConnectionManager          — MCP server lifecycle
├── hookEngine: HookEngine              — Session-level hooks
├── store: SessionStore                — Persistence backend
├── rpc: SDKSessionRPC                 — RPC interface for SDK
│
├── config: SessionConfig
│   ├── runtime: RuntimeConfig
│   ├── providerManager: ProviderManager
│   ├── mcpConfig: SessionMcpConfig
│   └── permissionRules: PermissionRule[]
│
└── metadata: SessionMeta
    ├── title: string
    ├── created: Date
    ├── updated: Date
    └── customData: Record<string, unknown>
```

### SessionConfig

```typescript
interface SessionConfig {
  id: string;                              // UUID
  homedir: string;                         // Agent data directory
  runtime: RuntimeConfig;                  // Environment (kaos, env vars)
  rpc: [SDKRPC, SessionRPC];              // Bidirectional RPC pair
  providerManager: ProviderManager;        // LLM provider resolution
  hookEngine: HookEngine;                 // Event hooks
  permissionRules: PermissionRule[];       // Default permission rules
  mcpConfig: SessionMcpConfig;            // MCP server configuration
  skillRoots: SkillRoot[];                // Skill discovery directories
  defaultModel: string;                   // Fallback model
  log: Logger;                            // Diagnostic logger
}
```

### Session Lifecycle

```
Session.create(config)
    │
    ├── 1. Create session directory (if new)
    ├── 2. Initialize SkillRegistry → load all skill roots
    ├── 3. Initialize McpConnectionManager → connect MCP servers
    ├── 4. Create main Agent
    │       └── Agent.create(config, runtime)
    ├── 5. Fire SessionStart hook
    └── return Session

Session.resume(sessionId)
    │
    ├── 1. Load session state from state.json
    ├── 2. Initialize SkillRegistry
    ├── 3. Initialize McpConnectionManager
    ├── 4. Create main Agent
    ├── 5. Agent.resume() — replay wire.jsonl
    │       └── For each record: restoreAgentRecord()
    ├── 6. Fire SessionStart hook
    └── return Session

Session.close()
    │
    ├── 1. Fire SessionEnd hook
    ├── 2. Save metadata to state.json
    ├── 3. Close all agents
    ├── 4. Disconnect all MCP servers
    ├── 5. Flush records
    └── return
```

## Session Subagent Host

**Source**: `packages/agent-core/src/session/subagent-host.ts`

```typescript
class SessionSubagentHost {
  constructor(session: Session, parentAgent: Agent);

  /**
   * Spawn a new subagent with the given profile.
   * Creates a child Agent with inherited config (model, thinking, permissions).
   */
  spawn(profileName: string, options: RunSubagentOptions): Promise<SubagentHandle>;

  /**
   * Resume an existing subagent (from persisted state).
   */
  resume(agentId: string, options: RunSubagentOptions): Promise<SubagentHandle>;

  /**
   * Cancel all foreground subagents.
   */
  cancelAll(): void;
}

interface RunSubagentOptions {
  task: string;                    // Task description for the subagent
  signal?: AbortSignal;
  timeout?: number;
  parentTurnId: string;
  gitContext?: string;             // Git context for explore agents
}

interface SubagentHandle {
  agentId: string;
  promise: Promise<SubagentResult>;
}

interface SubagentResult {
  summary: string;                 // Concise summary of what the subagent did
  agentId: string;
  terminated: boolean;             // Was it cancelled?
}
```

### Subagent Spawn Flow

```
1. resolveProfile("explore")
   → DEFAULT_AGENT_PROFILES["explore"]
   → { systemPrompt, tools: [Read, Glob, Grep, ...] }

2. Create child Agent:
   agent = Session.createAgent({
     type: 'sub',
     profile: resolvedProfile,
     model: parent.config.model,
     thinking: parent.config.thinking,
   })

3. Run task to completion:
   result = await agent.turn.prompt(taskParts, { origin: 'subagent_spawn' })

4. Check summary length:
   if result.summary.length < 200 chars:
     agent.turn.prompt("Please provide a more detailed summary.")
     → Use continuation to get a better summary

5. Return { summary, agentId, terminated }
```

## Session Store (Persistence)

**Source**: `packages/agent-core/src/session/store/`

```typescript
class SessionStore {
  constructor(sessionsDir: string, kaos: Kaos);

  create(sessionId: string): Promise<SessionDir>;
  fork(sourceId: string, targetId: string): Promise<SessionDir>;
  get(sessionId: string): Promise<SessionDir | null>;
  list(workdir: string): Promise<SessionInfo[]>;
  find(sessionIdPrefix: string): Promise<SessionDir | null>;
  delete(sessionId: string): Promise<void>;
  rename(sessionId: string, title: string): Promise<void>;
  getIndex(): AsyncIterable<SessionIndexEntry>;
}

interface SessionDir {
  sessionId: string;
  path: string;                      // Full path to session directory
  agentHomedir(agentId: string): string;  // Path to specific agent's data dir
}

interface SessionInfo {
  sessionId: string;
  title: string;
  workdir: string;
  createdAt: Date;
  updatedAt: Date;
  agentCount: number;
}

interface SessionIndexEntry {
  sessionId: string;
  sessionDir: string;
  workDir: string;
}
```

### Directory Layout

```
sessions/
├── <workdir-key>/                  # Encoded working directory path
│   └── <session-uuid>/
│       ├── state.json              # { title, createdAt, updatedAt, agents: {...}, customData }
│       └── agents/
│           ├── main/
│           │   ├── wire.jsonl      # Event records (append-only JSON Lines)
│           │   └── plans/          # Plan files (created during plan mode)
│           └── <subagent-uuid>/
│               └── wire.jsonl
├── session_index.jsonl             # { sessionId, sessionDir, workDir }\n per line
```

The index is append-only JSONL for performance. Each entry records the session ID, its directory path, and the working directory. On session list, the index is scanned and existence-verified.

## RPC Protocol

**Source**: `packages/agent-core/src/rpc/`

The RPC system uses a **bidirectional method exchange** pattern. Two sides exchange method collections, and each can call methods on the other.

```typescript
function createRPC<Left, Right>(): [RPCClient<Left, Right>, RPCClient<Right, Left>];
```

- Side A can call methods on Side B via `rpc.methodName(args)`
- Side B can call methods on Side A via `reverseRpc.methodName(args)`
- Arguments and return values are serialized/deserialized automatically
- Errors are converted to `KimiErrorPayload` for safe transport

### Message Format

```typescript
type RpcResponse =
  | { ok: true; value: unknown }           // Success
  | { ok: false; error: KimiErrorPayload }; // Failure

interface KimiErrorPayload {
  message: string;
  code: string;        // e.g., "CONTEXT_OVERFLOW", "MAX_STEPS_EXCEEDED"
  stack?: string;      // Serialized stack trace
}
```

### CoreAPI (Top-Level)

```typescript
interface CoreAPI {
  // Session management
  createSession(options: CreateSessionOptions): Promise<string>;
  resumeSession(sessionId: string): Promise<string>;
  closeSession(sessionId: string): Promise<void>;
  forkSession(sessionId: string): Promise<string>;
  exportSession(sessionId: string): Promise<Uint8Array>;
  listSessions(workdir?: string): Promise<SessionInfo[]>;
  
  // Configuration
  getKimiConfig(): Promise<KimiConfig>;
  setKimiConfig(patch: Partial<KimiConfig>): Promise<void>;
  removeKimiProvider(name: string): Promise<void>;
  
  // System
  getCoreInfo(): Promise<CoreInfo>;
  reconnectMcpServer(sessionId: string, serverName: string): Promise<void>;
  getMcpStartupMetrics(sessionId: string): Promise<McpStartupMetrics>;
}
```

### AgentAPI (Per-Agent, Session-Scoped)

```typescript
interface AgentAPI {
  prompt(input: ContentPart[], origin: PromptOrigin): Promise<TurnEndResult>;
  steer(input: ContentPart[], origin: PromptOrigin): Promise<void>;
  cancel(): void;
  waitForCurrentTurn(signal?: AbortSignal): Promise<TurnEndResult>;
  
  setThinking(level: ThinkingEffort): void;
  setPermission(mode: PermissionMode): void;
  setModel(model: string): Promise<void>;
  
  enterPlan(): Promise<void>;
  cancelPlan(): void;
  clearPlan(): void;
  exitPlan(): void;
  
  compact(signal: AbortSignal, instruction?: string): Promise<void>;
  cancelCompaction(): void;
  clear(): void;
  
  registerTool(tool: UserToolDefinition): void;
  unregisterTool(name: string): void;
  setActiveTools(tools: string[]): void;
  
  stopBackground(taskId: string): void;
  waitBackgroundTask(taskId: string): Promise<void>;
  
  getConfig(): AgentConfigInfo;
  getContext(): ContextInfo;
  getTurnState(): TurnStateInfo;
  listBackgroundTasks(): BackgroundTaskInfo[];
  
  renameSession(title: string): Promise<void>;
  updateSessionMetadata(data: Record<string, unknown>): Promise<void>;
  
  listMcpServers(): McpServerStatus[];
  listSkills(): SkillDefinition[];
  activateSkill(name: string, args: string): Promise<void>;
}
```

### SDKSessionRPC (Wraps AgentAPI with session/agent context)

```typescript
interface SDKSessionRPC {
  // Wraps AgentAPI methods — automatically injects sessionId + agentId
  prompt(input: ContentPart[], origin: PromptOrigin): Promise<TurnEndResult>;
  steer(input: ContentPart[], origin: PromptOrigin): Promise<void>;
  // ... all AgentAPI methods ...
  
  // Event subscription
  addEventListener<K extends AgentEventType>(type: K, listener: (event: AgentEvent) => void): void;
  removeEventListener<K extends AgentEventType>(type: K, listener: (event: AgentEvent) => void): void;
}
```

The `proxyWithExtraPayload` pattern automatically injects `sessionId` and `agentId` into every RPC call, so the SDK consumer never needs to specify them:

```typescript
// SDK consumer writes:
session.agent.prompt(input, origin);
// RPC layer automatically sends:
// { method: 'prompt', args: [input, origin], extra: { sessionId, agentId } }
```

### Event Types (30+)

Key event categories:

| Category | Events |
|----------|--------|
| Turn lifecycle | `turn.started`, `turn.ended`, `turn.step.started`, `turn.step.completed` |
| Streaming | `assistant.delta`, `thinking.delta`, `tool.call.started`, `tool.result`, `tool.progress` |
| Subagent | `subagent.spawned`, `subagent.completed`, `subagent.failed` |
| MCP | `mcp.server.status`, `tool.list.updated` |
| Compaction | `compaction.started`, `compaction.completed` |
| Background | `background.started`, `background.completed` |
| System | `error`, `agent.status.updated` |

## KimiCore (Implementation)

**Source**: `packages/agent-core/src/rpc/core-impl.ts`

`KimiCore` is the server-side implementation of `CoreAPI`:

```typescript
class KimiCore {
  private sessions: Map<string, Session> = new Map();
  private config: KimiConfig;
  
  async createSession(options): Promise<string> {
    const session = new Session({ ...options });
    await session.initialize();
    this.sessions.set(session.id, session);
    return session.id;
  }
  
  async resumeSession(sessionId): Promise<string> {
    const session = await Session.resume(sessionId);
    this.sessions.set(session.id, session);
    return session.id;
  }
  
  // ... etc
}
```

## Re-implementation Notes

1. **RPC is the contract between engine and UI**: All communication between the agent and the TUI goes through RPC methods and events. Porting the RPC protocol definition is essential for any reimplementation.

2. **Session manages three resource types**: Agents, MCP connections, and skills. All three need proper cleanup on session close.

3. **Session persistence is file-based**: No database. Directories + JSON files + JSONL. Portable to any platform.

4. **Subagent spawn creates a full Agent instance**: This means each subagent has its own context window, tool set, and records. This is expensive — only spawn subagents for non-trivial tasks.

5. **The wire.jsonl format is the single source of truth**: Session resume reads all records from wire.jsonl and applies them via `restoreAgentRecord()`. This is the event sourcing pattern. The session index (session_index.jsonl) is a fast-lookup cache, not the source of truth.

6. **Events are fire-and-forget**: The Agent emits events but doesn't wait for acknowledgment. The TUI subscribes to events and renders them as they arrive. If the TUI falls behind, events are buffered in the RPC channel.
