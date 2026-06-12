# Architecture Overview

Kimi Code CLI is an AI coding agent that runs in the terminal. This document describes the high-level architecture.

## System Boundaries

```
┌────────────────────────────────────────────────────────────────────┐
│                         USER / TERMINAL                           │
│                    (types prompts, sees TUI)                      │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  CLI/TUI Layer (apps/kimi-code)                                   │
│  - Command-line parsing                                           │
│  - Terminal UI (pi-tui framework)                                 │
│  - Reverse-RPC for approvals/questions                            │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ RPC (in-process)
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  SDK Layer (packages/kimi-code-sdk)                               │
│  - KimiHarness (entry point)                                      │
│  - Session management                                               │
│  - Event subscription                                               │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  Agent Core (packages/agent-core)                                 │
│                                                                     │
│  ┌───────────┐    ┌───────────┐    ┌────────────────────────────┐ │
│  │  Session  │───>│   Agent   │───>│        TurnFlow            │ │
│  │ (manager) │    │(orchestr.)│    │  (prompt/steer/cancel)     │ │
│  └─────┬─────┘    └─────┬─────┘    └───────────┬────────────────┘ │
│        │                │                       │                  │
│        │                │                       ▼                  │
│        │                │            ┌────────────────────┐        │
│        │                │            │  runTurn (Loop)    │        │
│        │                │            │  LLM → Tools loop  │        │
│        │                │            └────────────────────┘        │
│        │                │                       │                  │
│        │                │          ┌───────────┼──────────┐       │
│        │                │          ▼           ▼          ▼      │
│        │                │    ┌────────┐ ┌──────────┐ ┌─────────┐ │
│        │                │    │ Tools  │ │ Perm.    │ │ Hooks   │ │
│        │                │    └───┬────┘ └──────────┘ └─────────┘ │
│        │                │        │                               │
│        │                │        ▼                               │
│        │                │  ┌────────────────┐                   │
│        │                │  │ kaos (I/O)     │──> filesystem     │
│        │                │  │ kosong (LLM)   │──> HTTP APIs       │
│        │                │  │ MCP Manager    │──> ext. servers    │
│        │                │  └────────────────┘                   │
│        │                │                                        │
│        │                │  ┌──────────────┐                     │
│        │                │  │ AgentRecords │──> wire.jsonl        │
│        │                │  └──────────────┘                     │
└────────┼────────────────┼────────────────────────────────────────┘
         │                │
         ▼                ▼
┌────────────────────────────────────────────────────────────────────┐
│  Infrastructure Layer                                               │
│  - Kaos: Filesystem/process abstraction                           │
│  - Kosong: LLM provider abstraction (Anthropic, OpenAI, etc.)     │
│  - MCP: Model Context Protocol client                             │
└────────────────────────────────────────────────────────────────────┘
```

## Core Data Flow

```
User Input
    │
    ▼
┌─────────────┐
│ TurnFlow    │
│ .prompt()   │
└──────┬──────┘
       │
       ▼
┌────────────────────────────────────────────────────┐
│ runTurn() — Stateless Loop                         │
│                                                    │
│  repeat:                                           │
│    beforeStep() ──> executeLoopStep()            │
│                          │                         │
│               ┌──────────┴──────────┐             │
│               ▼                     ▼             │
│         LLM.chat()             tool execution     │
│               │                     │             │
│               ▼                     ▼             │
│         streamed response      ToolResults        │
│               │                     │             │
│               ▼                     ▼             │
│         stopReason:            stopReason:        │
│         end_turn ───> EXIT    tool_use ───> loop │
│                                                    │
└────────────────────────────────────────────────────┘
       │
       ▼
TurnEndResult → Events → TUI
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
                    │  kimi-code-sdk      │
                    └──────────┬──────────┘
                               │ depends on
                               ▼
    ┌─────────────────────────────────────────────────┐
    │              agent-core                         │
    │  (agent, session, loop, tools, MCP, RPC,        │
    │   permissions, hooks, skills, records)         │
    └────┬──────────┬──────────┬──────────┬───────────┘
         │          │          │          │
         ▼          ▼          ▼          ▼
    ┌───────┐ ┌─────────┐ ┌─────────┐ ┌──────────────┐
    │ kaos  │ │ kosong  │ │ oauth   │ │ telemetry    │
    │ (I/O) │ │ (LLM)   │ │ (auth)  │ │ (observ.)    │
    └───────┘ └─────────┘ └─────────┘ └──────────────┘
```

## Key Design Decisions

### 1. Event Sourcing

All state changes are recorded as append-only events in `wire.jsonl`. This enables:
- Session replay and crash recovery
- Debugging by inspecting event sequence
- Session export for sharing

```
Agent action
    │
    ├── logRecord({ type: 'turn.prompt', ... })
    ├── Execute action
    ├── logRecord({ type: 'context.append_message', ... })
    │
    └── All records → wire.jsonl
```

### 2. Two-Phase Tool Execution

Tools implement `resolveExecution()` (pure validation) and `execute()` (side effects):
- Permission checking happens between phases
- UI can show what will happen before it happens
- Enables cancellation before I/O

### 3. Stateless Loop

The `runTurn()` function has no hidden state:
```typescript
async function runTurn(input: {
  turnId: string;
  signal: AbortSignal;
  llm: LLM;
  buildMessages: () => Message[];
  dispatchEvent: LoopEventDispatcher;
  tools?: ExecutableTool[];
  hooks?: LoopHooks;
  maxSteps?: number;
}): Promise<TurnResult>
```

This makes the loop testable and portable.

### 4. Kaos Abstraction

All filesystem and process operations go through the `Kaos` interface:
- `LocalKaos` for local machine
- `SSHKaos` for remote machines
- Makes tools testable and portable

## Event Flow

```
Agent ──emit──> AgentEvent ──> RPC ──> SDK ──> TUI.render()
  │                                                │
  │  Events: turn.started, turn.ended,            │
  │  assistant.delta, tool.call.started,           │
  │  tool.result, thinking.delta,                  │
  │  error, agent.status.updated,                  │
  │  subagent.spawned/completed/failed             │
  │                                                │
  └──record──> AgentRecords ──> wire.jsonl        │
                                                   │
  (Approval/Question RPC)                          │
  TUI ──requestApproval──> Agent                   │
  TUI ──askQuestion──────> Agent                   │
```

## Session Persistence Layout

```
~/.kimi-code/
├── config.toml                        # User configuration
├── mcp.json                           # MCP server config
├── session_index.jsonl                # Session index
├── sessions/
│   └── <workdir-key>/
│       └── <session-uuid>/
│           ├── state.json             # Session metadata
│           └── agents/
│               ├── main/
│               │   └── wire.jsonl     # Event records
│               └── <subagent-id>/
│                   └── wire.jsonl
└── credentials/
    └── mcp/                           # MCP OAuth tokens
```

## Re-implementation Roadmap

| Phase | Modules | Dependencies |
|-------|---------|--------------|
| **1: Foundation** | Kaos → Config → Kosong | None → Config → HTTP |
| **2: Agent Core** | Loop → Agent → Context → Turn | Phase 1 |
| **3: Services** | Records → Session → Tools → Permission/Hooks | Phase 2 |
| **4: Extensions** | Skills → MCP | Phase 3 |
| **5: Application** | CLI/TUI → SDK | Phase 4 |

See [exec-plans/active/](exec-plans/active/) for detailed implementation plans.

## References

- [design-docs/core-beliefs.md](design-docs/core-beliefs.md) — Design principles
- [design-docs/system-overview.md](design-docs/system-overview.md) — System boundaries
- [references/index.md](references/index.md) — Module reference
