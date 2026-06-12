# Node SDK (Public API)

## Core Purpose

Public TypeScript API for embedding Kimi Code Agent in other applications.

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
    │   ├── agent.*         → Agent control
    │   └── addEventListener → Event subscription
    │
    └── KimiAuthFacade      → Authentication
        ├── login()          → OAuth device flow
        └── logout()         → Clear credentials
```

## KimiHarness

```typescript
class KimiHarness {
  constructor(options: KimiHarnessOptions);
  
  createSession(options?: CreateSessionOptions): Promise<Session>;
  resumeSession(sessionId: string): Promise<Session>;
  closeSession(sessionId: string): Promise<void>;
  forkSession(sessionId: string): Promise<Session>;
  listSessions(workdir?: string): Promise<SessionInfo[]>;
  
  get auth(): KimiAuthFacade;
  static getBuildInfo(): BuildInfo;
  shutdown(): Promise<void>;
}
```

## Session

```typescript
class Session {
  readonly id: string;
  readonly agent: AgentAPI;
  
  rename(title: string): Promise<void>;
  export(): Promise<Uint8Array>;
  
  addEventListener(type: string, listener: Function): void;
  removeEventListener(type: string, listener: Function): void;
  close(): Promise<void>;
}
```

## AgentAPI

```typescript
interface AgentAPI {
  prompt(input: string | ContentPart[]): Promise<void>;
  steer(input: string | ContentPart[]): Promise<void>;
  cancel(): Promise<void>;
  
  setModel(model: string): Promise<void>;
  setPermissionMode(mode: PermissionMode): Promise<void>;
  
  approveToolCall(toolCallId: string, approved: boolean): Promise<void>;
  answerQuestion(questionId: string, answer: string): Promise<void>;
  
  get usage(): TokenUsage;
  get status(): AgentStatus;
}
```

## Event Types

| Event | Description |
|-------|-------------|
| `turn.started` | Turn began |
| `turn.ended` | Turn completed |
| `assistant.delta` | Text streaming |
| `thinking.delta` | Thinking streaming |
| `tool.call.started` | Tool invocation started |
| `tool.result` | Tool result received |
| `error` | Error occurred |

## Auth Facade

```typescript
interface KimiAuthFacade {
  login(): Promise<void>;      // OAuth device flow
  logout(): Promise<void>;
  resolveToken(): Promise<string>;
  isAuthenticated(): boolean;
}
```

## File Path

```
packages/kimi-code-sdk/src/
├── kimi-harness.ts      # Main entry point
├── session.ts           # Session wrapper
├── auth.ts              # Auth facade
├── rpc-client.ts        # RPC client
└── events.ts            # Event types
```

---

**See also**: [16-node-sdk.md](../16-node-sdk.md) for full details.
