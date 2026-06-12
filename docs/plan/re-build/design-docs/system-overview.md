# System Overview

High-level system boundaries and component overview.

## What Is Kimi Code CLI?

An AI coding agent that runs in the terminal. It reads and edits code, executes shell commands, searches files and the web, and autonomously plans and adjusts its next actions based on feedback.

## System Boundaries

```
┌────────────────────────────────────────────────────────────────────┐
│                         USER / TERMINAL                           │
│                    (types prompts, sees TUI)                      │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  CLI/TUI Layer                                                    │
│  - Command-line parsing                                           │
│  - Terminal UI (pi-tui framework)                                 │
│  - Reverse-RPC for approvals/questions                            │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ RPC (in-process)
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  SDK Layer                                                        │
│  - KimiHarness (entry point)                                      │
│  - Session management                                               │
│  - Event subscription                                               │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  Agent Core                                                       │
│  - Session: Top-level manager                                     │
│  - Agent: Core orchestrator                                         │
│  - TurnFlow: Turn lifecycle                                         │
│  - Loop: Turn execution engine                                      │
│  - Tools: Action primitives                                         │
│  - Context: Conversation memory                                     │
│  - Records: Event sourcing                                          │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  Infrastructure Layer                                               │
│  - Kaos: Filesystem/process abstraction                           │
│  - Kosong: LLM provider abstraction                                 │
│  - MCP: Model Context Protocol client                               │
└────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | Key Class |
|-----------|----------------|-----------|
| **Session** | Owns agent lifecycle, MCP connections, skills | `Session` |
| **Agent** | Orchestrates all subsystems | `Agent` |
| **TurnFlow** | Manages turn lifecycle (prompt/steer/cancel) | `TurnFlow` |
| **Loop** | Executes LLM-tool interaction cycle | `runTurn()` |
| **Tools** | Defines what the agent can do | `ToolManager` |
| **Context** | Manages conversation history | `ContextMemory` |
| **Records** | Persists events to `wire.jsonl` | `AgentRecords` |
| **Kaos** | Abstracts filesystem/process I/O | `Kaos` interface |
| **Kosong** | Abstracts LLM providers | `ChatProvider` interface |

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Runtime | Node.js ≥ 24.15.0 |
| Language | TypeScript 6.0.2 (strict mode, ESM) |
| Package Manager | pnpm 10+ (workspace monorepo) |
| TUI Framework | `@earendil-works/pi-tui` |
| LLM SDKs | Anthropic SDK, OpenAI SDK, Google GenAI SDK |

## Package Structure

```
kimi-code/
├── apps/
│   ├── kimi-code/          # Main CLI/TUI application
│   └── vis/                # Debug visualization web app
├── packages/
│   ├── kaos/               # Execution environment abstraction
│   ├── kosong/             # LLM provider abstraction
│   ├── agent-core/         # Core agent engine
│   ├── kimi-telemetry/     # Telemetry infrastructure
│   ├── kimi-code-oauth/    # OAuth authentication
│   └── kimi-code-sdk/      # Public TypeScript SDK
└── docs/                   # Documentation site
```

## Key Design Patterns

| Pattern | Where Used | Rationale |
|---------|-----------|-----------|
| Event Sourcing | `AgentRecords` / `wire.jsonl` | Session replay, crash recovery |
| Strategy | `ChatProvider` implementations | Pluggable LLM providers |
| Proxy | RPC layer | Bidirectional method exchange |
| Observer | `AgentEvent` system | Real-time UI updates |
| Dependency Injection | Session → Agent → subsystems | Testability |
| Adapter | Kosong providers | Uniform interface |
| Two-Phase Execution | `resolveExecution()` + `.execute()` | Permission before I/O |

## See Also

- [core-beliefs.md](core-beliefs.md) — Design principles
- [data-flow.md](data-flow.md) — Detailed data flows
- [../ARCHITECTURE.md](../ARCHITECTURE.md) — Architecture overview
