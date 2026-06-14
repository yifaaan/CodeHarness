# Node SDK

**Source**: `packages/kimi-code-sdk/src/`

## Purpose

The SDK (`@moonshot-ai/kimi-code-sdk`) is the **public TypeScript API** for embedding Kimi Code Agent in other applications. It provides a clean, stable interface for session management, agent communication, authentication, and event subscription — hiding the complexity of the internal RPC protocol, session persistence, and provider management.

The SDK is what the CLI app uses internally, and what external consumers would use to build custom integrations.

## Architecture

```
External Consumer
    │
    ├── KimiHarness         → Main entry point
    │   ├── createSession() → Creates new agent session
    │   ├── resumeSession() → Resumes existing session
    │   └── closeSession()  → Closes session
    │
    ├── Session wrapper     → Per-session operations
    │   ├── agent.*         → Agent control (prompt, steer, setModel, etc.)
    │   ├── addEventListener → Event subscription
    │   └── removeEventListener → Event unsubscription
    │
    └── KimiAuthFacade      → Authentication
        ├── login()          → OAuth device flow
        ├── logout()         → Clear credentials
        └── resolveToken()   → Get auth token
```

## KimiHarness

**Source**: `packages/kimi-code-sdk/src/kimi-harness.ts`

```typescript
class KimiHarness {
  constructor(options: KimiHarnessOptions);

  /** Create a new agent session */
  async createSession(options?: CreateSessionOptions): Promise<Session>;

  /** Resume an existing session */
  async resumeSession(sessionId: string): Promise<Session>;

  /** Close a session */
  async closeSession(sessionId: string): Promise<void>;

  /** Fork a session (clone with full history) */
  async forkSession(sessionId: string): Promise<Session>;

  /** List all sessions for a working directory */
  async listSessions(workdir?: string): Promise<SessionInfo[]>;

  /** Get authentication facade */
  get auth(): KimiAuthFacade;

  /** Get build/version info */
  static getBuildInfo(): BuildInfo;

  /** Shutdown the harness */
  async shutdown(): Promise<void>;
}

interface KimiHarnessOptions {
  homeDir?: string;               // Override KIMI_CODE_HOME
  configPath?: string;            // Override config file path
  identity?: KimiIdentity;        // User identity for telemetry
  uiMode?: 'shell' | 'headless';  // UI mode
  telemetry?: boolean;            // Enable telemetry
  skillDirs?: string[];           // Additional skill directories
  logLevel?: string;              // Logging level
}

interface CreateSessionOptions {
  workdir?: string;               // Working directory
  model?: string;                 // Model alias
  planMode?: boolean;             // Start in plan mode
  yoloMode?: boolean;             // Start in YOLO mode
  permissionMode?: PermissionMode;
  parentSessionId?: string;       // For session linking
}
```

### Harness Lifecycle

```
KimiHarness constructed:
  1. Resolve config (homeDir, configPath)
  2. Initialize logging (per log level)
  3. Create ProviderManager (reads config.toml)
  4. Create RPC pair (in-process)
  5. Initialize KimiCore (server side of RPC)
  6. Initialize telemetry
  7. Set up auth facade

Harness.createSession():
  1. Generate session ID (UUID)
  2. Call KimiCore.createSession(sessionId, options)
  3. Session initializes: skills, MCP, main agent
  4. Return Session wrapper

Harness.closeSession(sessionId):
  1. Call Session.close()
  2. Persist session metadata
  3. Disconnect MCP servers
  4. Remove from active sessions

Harness.shutdown():
  1. Close all active sessions
  2. Flush telemetry events
  3. Clean up temporary resources
```

## Session Wrapper

**Source**: `packages/kimi-code-sdk/src/session.ts`

```typescript
class Session {
  /** Session ID */
  readonly id: string;

  /** Agent RPC interface (main agent) */
  readonly agent: AgentAPI;

  /** Create a new session (internal) */
  static create(harness: KimiHarness, options: CreateSessionOptions): Promise<Session>;

  /** Resume a session from persisted state */
  static resume(harness: KimiHarness, sessionId: string): Promise<Session>;

  /** Rename the session */
  async rename(title: string): Promise<void>;

  /** Export the session as a ZIP */
  async export(): Promise<Uint8Array>;

  /** Subscribe to agent events */
  addEventListener<K extends AgentEventType>(type: K, listener: (event: AgentEvent) => void): void;

  /** Unsubscribe from agent events */
  removeEventListener<K extends AgentEventType>(type: K, listener: (event: AgentEvent) => void): void;

  /** Close this session */
  async close(): Promise<void>;
}
```

### AgentAPI (from SDK Perspective)

The `session.agent` object exposes the AgentAPI through the RPC proxy:

```typescript
// Usage:
const session = await harness.createSession();
await session.agent.prompt(input, origin);
session.agent.setModel('claude-sonnet-4');

// Event subscription:
session.addEventListener('assistant.delta', (event) => {
  process.stdout.write(event.content.text);
});
session.addEventListener('tool.call.started', (event) => {
  console.log(`Tool: ${event.toolCall.function.name}`);
});
session.addEventListener('turn.ended', (event) => {
  console.log('Turn complete');
});
```

## Auth Facade

**Source**: `packages/kimi-code-sdk/src/auth.ts`

```typescript
class KimiAuthFacade {
  constructor(options: AuthOptions);

  /** Login with OAuth device flow */
  async login(provider?: string): Promise<void>;

  /** Login with API key */
  async loginWithApiKey(provider: string, apiKey: string): Promise<void>;

  /** Logout (clear credentials) */
  async logout(provider?: string): Promise<void>;

  /** Check if logged in */
  async isLoggedIn(provider?: string): Promise<boolean>;

  /** Resolve OAuth token for a provider (used by ProviderManager) */
  resolveOAuthTokenProvider(providerName: string): ProviderRequestAuth | null;
}
```

### Token Storage

- OAuth tokens stored in `~/.kimi-code/credentials/` (file-based)
- Uses `proper-lockfile` for concurrent access safety
- Supports multiple providers simultaneously

## RPC Client

**Source**: `packages/kimi-code-sdk/src/rpc.ts`

```typescript
class RpcClient implements SDKRPC {
  constructor(rpc: RPCClient);

  /** Call a method on the agent */
  async call(method: string, ...args: unknown[]): Promise<unknown>;

  /** Subscribe to events */
  addEventListener(type: string, listener: Function): void;

  /** Unsubscribe from events */
  removeEventListener(type: string, listener: Function): void;
}
```

The RPC client wraps the bidirectional RPC pair. It:
1. Serializes method calls with `sessionId` and `agentId` injected automatically
2. Deserializes responses and error payloads
3. Routes events to registered listeners
4. Handles reconnection (for long-running sessions)

## Event Types

**Source**: `packages/kimi-code-sdk/src/events.ts`

```typescript
// All event types re-exported from agent-core
export type {
  AgentEvent,
  AgentEventType,
  TurnStartedEvent,
  TurnEndedEvent,
  TurnStepStartedEvent,
  TurnStepCompletedEvent,
  AssistantDeltaEvent,
  ThinkingDeltaEvent,
  ToolCallStartedEvent,
  ToolResultEvent,
  ToolProgressEvent,
  ErrorEvent,
  AgentStatusUpdatedEvent,
  SubagentSpawnedEvent,
  SubagentCompletedEvent,
  CompactionStartedEvent,
  CompactionCompletedEvent,
  BackgroundStartedEvent,
  BackgroundCompletedEvent,
  McpServerStatusEvent,
  ToolListUpdatedEvent,
  SkillInvokedEvent,
} from '@moonshot-ai/agent-core';
```

## TypeScript Types

**Source**: `packages/kimi-code-sdk/src/types.ts`

```typescript
// Re-exports all public types for SDK consumers
export type { ContentPart, TextPart, ThinkPart, ToolCall } from '@moonshot-ai/llm';
export type { TokenUsage, FinishReason } from '@moonshot-ai/llm';
export type { PermissionMode, PermissionRule } from '@moonshot-ai/agent-core';
export type { TurnEndResult, TurnStateInfo } from '@moonshot-ai/agent-core';
export type { SessionInfo, CoreInfo } from '@moonshot-ai/agent-core';
export type { KimiConfig, ProviderConfig, ModelAlias } from '@moonshot-ai/agent-core';
export type { BuildInfo } from '@moonshot-ai/agent-core';

// Error types
export { KimiError, KimiErrorPayload, ErrorCodes } from '@moonshot-ai/agent-core';
```

## Re-implementation Notes

1. **The SDK is the public API**: It wraps all internal complexity behind a clean interface. For a reimplementation, the SDK is the last thing to port — all internal modules must be working first.

2. **KimiHarness is the composition root**: It initializes config, provider manager, RPC, and session management. Porting the harness means porting all these dependencies.

3. **The Session wrapper is thin**: It's primarily a proxy that adds `sessionId` to RPC calls and manages event listeners. The real work is in `KimiCore` (agent-core).

4. **Events are the primary communication channel**: The addEventListener pattern is how consumers observe agent activity. All 20+ event types must be preserved for UI rendering.

5. **Auth facade is standalone**: `KimiAuthFacade` only depends on the OAuth package. It can be ported independently of the agent engine.

6. **Type re-exports**: The SDK re-exports types from llm and agent-core. For another language, these types would need to be defined in the SDK itself (or the equivalent of "types" package).