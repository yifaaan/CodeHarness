# Architecture Overview

## What Is Kimi Code CLI?

Kimi Code CLI is an **AI coding agent** that runs in the terminal. It reads and edits code, executes shell commands, searches files and the web, and autonomously plans and adjusts its next actions based on feedback — all within an interactive TUI (Terminal UI) session.

**System role**: A conversational AI agent that:
1. Receives natural language prompts from a user in a terminal
2. Plans and executes multi-step software engineering tasks
3. Calls LLMs (Large Language Models) to generate responses and decide actions
4. Executes tools (file I/O, shell, web search, subagent delegation)
5. Persists session state for replay and resumption
6. Supports extension via MCP (Model Context Protocol) servers and custom skills

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Runtime | Node.js ≥ 24.15.0 |
| Language | TypeScript 6.0.2 (strict mode, ESM) |
| Package Manager | pnpm 10+ (workspace monorepo) |
| Build | tsdown (TypeScript bundler to ESM) |
| Test | Vitest 4.x |
| Lint/Format | oxlint 1.59.0 |
| Versioning | Changesets |
| TUI Framework | `@earendil-works/pi-tui` |
| LLM SDKs | Anthropic SDK, OpenAI SDK, Google GenAI SDK |

## High-Level Data Flow

```
┌────────────────────────────────────────────────────────────────────┐
│                         TERMINAL USER                             │
│   types `kimi` → sees TUI → types prompt → sees responses         │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ stdin/stdout
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│  apps/kimi-code (CLI App)                                          │
│                                                                     │
│  ┌──────────┐   ┌──────────────┐   ┌──────────────────────────┐    │
│  │ main.ts  │──>│ KimiHarness  │──>│      KimiTUI (TUI)       │    │
│  │ (entry)  │   │ (SDK)        │   │ ┌──────────────────────┐ │    │
│  └──────────┘   └──────┬───────┘   │ │ Reverse-RPC handlers │ │    │
│                         │           │ │ (events → rendering) │ │    │
│                         │           │ └──────────────────────┘ │    │
│                         │           └──────────────────────────┘    │
└─────────────────────────┼───────────────────────────────────────────┘
                          │ RPC (in-process method calls)
                          ▼
┌─────────────────────────────────────────────────────────────────────┐
│  packages/agent-core (Core Engine)                                  │
│                                                                     │
│  ┌───────────┐    ┌───────────┐    ┌────────────────────────────┐  │
│  │  Session  │───>│   Agent   │───>│        TurnFlow            │  │
│  │ (manager) │    │(orchestr.)│    │  (prompt/steer/cancel)     │  │
│  └─────┬─────┘    └─────┬─────┘    └───────────┬────────────────┘  │
│        │                │                       │                   │
│        │                │                       ▼                   │
│        │                │            ┌────────────────────┐        │
│        │                │            │  runTurn (Loop)    │        │
│        │                │            │  ┌──────────────┐  │        │
│        │                │            │  │ executeStep   │  │        │
│        │                │            │  │ LLM → tools   │  │        │
│        │                │            │  │ repeat until  │  │        │
│        │                │            │  │ end_turn     │  │        │
│        │                │            │  └──────────────┘  │        │
│        │                │            └────────────────────┘        │
│        │                │                       │                  │
│        │                │          ┌────────────┼──────────┐       │
│        │                │          ▼            ▼          ▼      │
│        │                │    ┌────────┐ ┌──────────┐ ┌─────────┐  │
│        │                │    │ Tools  │ │ Perm.    │ │ Hooks   │  │
│        │                │    └───┬────┘ └──────────┘ └─────────┘  │
│        │                │        │                                 │
│        │                │        ▼                                 │
│        │                │  ┌────────────────┐                     │
│        │                │  │ kaos (I/O)     │                     │
│        │                │  │ LocalKaos      │──> filesystem       │
│        │                │  │ SSHKaos        │──> remote SSH       │
│        │                │  └────────────────┘                     │
│        │                │                                          │
│        │                │  ┌──────────────────┐                   │
│        │                │  │ kosong (LLM)     │                   │
│        │                │  │ ChatProvider     │──> Anthropic      │
│        │                │  │                  │──> OpenAI         │
│        │                │  │                  │──> Google GenAI   │
│        │                │  │                  │──> Kimi           │
│        │                │  └──────────────────┘                   │
│        │                │                                          │
│        │                │  ┌──────────────────┐                   │
│        │                │  │ MCP Manager      │──> external MCP   │
│        │                │  │                  │    servers         │
│        │                │  └──────────────────┘                   │
│        │                │                                          │
│        │                │  ┌──────────────┐                       │
│        │                │  │ AgentRecords │──> wire.jsonl          │
│        │                │  │ (event src.) │    (persistence)       │
│        │                │  └──────────────┘                       │
│        │                └──────────────────────────────────────────│
│        │                                                            │
│        │  ┌───────────────┐  ┌───────────────┐  ┌──────────────┐  │
│        │  │ SkillRegistry │  │ SessionStore  │  │ HookEngine   │  │
│        │  └───────────────┘  └───────────────┘  └──────────────┘  │
│        │                                                            │
│        │  ┌────────────────┐  ┌────────────────┐                   │
│        │  │ OAuth (pack.)  │  │ Telemetry(pack)│                   │
│        │  └────────────────┘  └────────────────┘                   │
└────────┼───────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────────┐
│  packages/kaos                       packages/kosong               │
│  ┌────────────┐                      ┌──────────────────┐         │
│  │ LocalKaos  │                      │ AnthropicProvider│──> HTTP │
│  ├────────────┤                      ├──────────────────┤──> HTTP │
│  │ SSHKaos    │                      │ OpenAIProvider   │──> HTTP │
│  └────────────┘                      ├──────────────────┤──> HTTP │
│                                       │ GoogleGenAI     │──> HTTP │
│                                       ├──────────────────┤──> HTTP │
│                                       │ KimiProvider     │──> HTTP │
│                                       └──────────────────┘         │
└────────────────────────────────────────────────────────────────────┘
```

## Package Dependency Graph

```
                    ┌─────────────────────┐
                    │   apps/kimi-code    │
                    │   (CLI + TUI)       │
                    └──────────┬──────────┘
                               │ depends on
                               ▼
                    ┌─────────────────────┐
                    │  kimi-code-sdk      │  (public SDK)
                    └──────────┬──────────┘
                               │ depends on
                               ▼
    ┌─────────────────────────────────────────────────┐
    │              agent-core                         │
    │  (agent, session, loop, tools, MCP, RPC,        │
    │   permissions, hooks, skills, records, context)  │
    └────┬──────────┬──────────┬──────────┬───────────┘
         │          │          │          │
         ▼          ▼          ▼          ▼
    ┌───────┐ ┌─────────┐ ┌─────────┐ ┌──────────────┐
    │ kaos  │ │ kosong  │ │ oauth   │ │ telemetry    │
    │ (I/O) │ │ (LLM)   │ │ (auth)  │ │ (observ.)    │
    └───────┘ └─────────┘ └─────────┘ └──────────────┘
         │          │
         │          │                ┌─────────────────┐
         │          └────────────────│ anthropic SDK   │
         │                           │ openai SDK      │
         │                           │ @google/genai   │
         │                           └─────────────────┘
         ▼
    ┌──────────────────────────────┐
    │  Node.js APIs (fs, child_process, path, crypto, http) │
    └──────────────────────────────┘
```

## Key Design Patterns

| Pattern | Where Used | Rationale |
|---------|-----------|-----------|
| **Event Sourcing** | `AgentRecords` / `wire.jsonl` | Every state change is recorded as an append-only event. Enables session replay and crash recovery without a database. |
| **Strategy** | `ChatProvider` implementations, `CompactionStrategy`, `PermissionPolicy` | Pluggable algorithms for LLM providers, compaction triggers, and permission decisions. |
| **Proxy** | RPC layer (`createRPC()`) | Bidirectional method exchange enables in-process IPC with automatic serialization. SDK and TUI communicate via RPC methods. |
| **Observer** | `AgentEvent` system, `McpConnectionManager.onStatusChange()` | Real-time UI updates without polling. Agent emits events, TUI renders them. |
| **Dependency Injection** | Session → Agent → all subsystems | All subsystems receive their parent Agent via constructor. This makes every subsystem independently testable. |
| **Adapter** | Kosong providers (AnthropicAdapter, OpenAIAdapter, etc.) | Each LLM provider wraps its SDK in a uniform `ChatProvider` interface. |
| **Two-Phase Execution** | `ExecutableTool.resolveExecution()` + `.execute()` | Pure validation phase then side-effectful execution. Enables permission checking before actual I/O. |
| **Stateless Loop** | `runTurn()` in `loop/` | All state is injected; the loop function has no side effects except through hooks. Testable and portable. |

## Event Architecture

The system uses a typed event system for real-time communication:

```
Agent ──emit──> AgentEvent ──> RPC ──> SDK ──> KimiTUI.render()
  │                                                │
  │  Events: turn.started, turn.ended,             │
  │  assistant.delta, tool.call.started,           │
  │  tool.result, thinking.delta,                  │
  │  error, agent.status.updated,                  │
  │  subagent.spawned/completed/failed             │
  │                                                │
  └──record──> AgentRecords ──> wire.jsonl         │
                                                   │
  (Approval/Question RPC)                          │
  TUI ──requestApproval──> Agent                   │
  TUI ──askQuestion──────> Agent                   │
```

Events flow in two directions:
- **Agent → UI**: Status updates, streaming deltas, errors
- **UI → Agent**: Approval decisions, question answers, user input

## Configuration Flow

```
TOML file (~/.kimi-code/config.toml)
  │
  ▼
KimiConfig (zod-validated schema)
  │
  ├──> ProviderManager.resolveProviderForModel("claude-sonnet-4")
  │      │
  │      ├──> Lookup model alias in [models] table
  │      ├──> Resolve provider config from [providers] table
  │      ├──> Resolve OAuth token if configured
  │      └──> Return RuntimeProvider (wraps ChatProvider)
  │
  ├──> PermissionConfig → PermissionManager
  ├──> HookConfig → HookEngine
  ├──> MCPConfig → McpConnectionManager
  └──> BackgroundConfig → BackgroundManager
```

## Session Persistence Layout

```
~/.kimi-code/                          # KIMI_CODE_HOME (default)
├── config.toml                        # User configuration
├── mcp.json                           # User-level MCP server config
├── session_index.jsonl                # Session index (append-only)
├── sessions/
│   └── <workdir-key>/                 # Key = encoded working directory
│       └── <session-uuid>/
│           ├── state.json             # Session metadata (title, timestamps)
│           └── agents/
│               ├── main/
│               │   └── wire.jsonl     # Event-sourced agent records
│               └── <subagent-id>/
│                   └── wire.jsonl     # Subagent records
├── credentials/
│   └── mcp/                           # MCP OAuth tokens
└── logs/                              # Diagnostic logs
```

## Re-implementation Notes

1. **Start with Kaos**: Every tool depends on the Kaos abstraction. Implement `LocalKaos` first — it only needs basic filesystem operations and process spawning.

2. **Config drives everything**: The TOML schema is the integration contract. Port the config schema and `ProviderManager` early — they determine how all other components are wired.

3. **Kosong providers are independent HTTP clients**: Each provider adapter is self-contained. Port the `ChatProvider` interface and one provider (OpenAI is the most standard) first, then add others.

4. **The loop is a pure function**: `runTurn()` takes all its dependencies as parameters. This is the easiest module to port and test in isolation.

5. **Agent construction order is load-bearing**: The order in which the `Agent` class initializes its subsystems matters because of cross-references. Preserve: records → compaction → context → config → turn → injection → permission → planMode → usage → tools → background → replayBuilder.

6. **The TUI is the most platform-specific**: The `pi-tui` framework would need a complete rewrite. For a reimplementation in another language, implement the CLI/TUI last, focusing on the RPC event types and rendering contract.

7. **wire.jsonl is portable JSON Lines**: The event-sourced record format is trivial to read/write in any language. No indexing, no mutation, append-only.
