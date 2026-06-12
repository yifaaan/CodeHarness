# Kimi Code CLI — Architecture Documentation Index

This directory contains comprehensive architecture documentation for the Kimi Code CLI project, organized as self-contained module references. Each document is designed to provide enough detail for reimplementing the module in another language.

## Monorepo Structure

```
kimi-code/                          # Root: pnpm workspace
├── apps/
│   ├── kimi-code/                  # Main CLI/TUI application (161 src files)
│   └── vis/                        # Debug visualization web app
│       ├── server/                 #   Hono backend
│       └── web/                    #   React 19 + Vite frontend
├── packages/
│   ├── kaos/                       # Execution environment abstraction
│   ├── kosong/                     # LLM provider abstraction
│   ├── agent-core/                 # Core agent engine (150+ files)
│   ├── kimi-telemetry/             # Telemetry infrastructure
│   ├── kimi-code-oauth/            # OAuth authentication
│   ├── kimi-code-sdk/              # Public TypeScript SDK
│   └── migration-legacy/           # Legacy data migration
├── docs/                           # VitePress documentation site
├── build/                          # Build utilities
└── .agents/skills/                 # Agent skills (gen-changesets, gen-docs, etc.)
```

## File Index

| # | File | Covers | Re-implementation Priority |
|---|------|--------|---------------------------|
| 01 | [01-architecture-overview.md](01-architecture-overview.md) | System overview, data flows, design patterns | **Start here** — understand the whole |
| 02 | [02-kaos-execution-layer.md](02-kaos-execution-layer.md) | `kaos` package: Kaos interface, LocalKaos, SSH, filesystem/process | **#1** — every tool depends on this |
| 03 | [03-kosong-llm-provider-layer.md](03-kosong-llm-provider-layer.md) | `kosong` package: ChatProvider, all 5 providers, streaming, capabilities | **#2** — core LLM abstraction |
| 04 | [04-config-and-provider-management.md](04-config-and-provider-management.md) | Config schema, ProviderManager, OAuth | **#3** — wires providers to config |
| 05 | [05-agent-core-engine.md](05-agent-core-engine.md) | `agent-core` Agent class: all subsystems, construction order, RPC methods | **#4** — the composition root |
| 06 | [06-agent-turn-flow.md](06-agent-turn-flow.md) | TurnFlow: prompt/steer/cancel, steer buffering, turn lifecycle | **#5** — user interaction entry |
| 07 | [07-session-and-rpc-layer.md](07-session-and-rpc-layer.md) | Session, SessionStore, RPC protocol, events, subagent host | **#6** — multi-agent + IPC |
| 08 | [08-loop-execution-engine.md](08-loop-execution-engine.md) | runTurn stateless loop, step execution, retry, tool scheduling | **#7** — the decision engine |
| 09 | [09-tool-system.md](09-tool-system.md) | ExecutableTool interface, ToolManager, ALL 15+ built-in tools | **#8** — the action system |
| 10 | [10-mcp-integration.md](10-mcp-integration.md) | MCP client, stdio/HTTP transports, OAuth, connection lifecycle | Parallel with #9 |
| 11 | [11-permission-and-hooks.md](11-permission-and-hooks.md) | PermissionManager, rules DSL, policies, HookEngine, all hook events | Parallel with #9 |
| 12 | [12-context-memory-and-compaction.md](12-context-memory-and-compaction.md) | ContextMemory, injection, FullCompaction strategy | Parallel with #9 |
| 13 | [13-skill-system.md](13-skill-system.md) | Skill parsing, discovery, registry, activation | Parallel with #9 |
| 14 | [14-agent-records-and-replay.md](14-agent-records-and-replay.md) | wire.jsonl event sourcing, replay, session export | Parallel with #9 |
| 15 | [15-cli-and-tui-layer.md](15-cli-and-tui-layer.md) | `kimi-code` app: CLI entry, TUI framework, reverse-RPC | Platform-specific (last) |
| 16 | [16-node-sdk.md](16-node-sdk.md) | `kimi-code-sdk`: KimiHarness, SDK session, auth facade | Last — public API wrapper |

## Dependency Graph (Module Level)

```
CLI/TUI (app) ────> SDK ────> Session ────> Agent ────> Loop ────> kosong (LLM)
                     │            │            │                    │
                     │            │            ├──> kaos (I/O)     │
                     │            │            ├──> Tools ───> kaos│
                     │            │            ├──> MCP            │
                     │            │            ├──> Permission     │
                     │            │            ├──> Hooks          │
                     │            │            ├──> Context        │
                     │            │            ├──> Compaction     │
                     │            │            ├──> Skills         │
                     │            │            └──> Records        │
                     │            │
                     │            └──> SessionStore (persistence)
                     │
                     ├──> OAuth
                     └──> Telemetry
```

## ASCII Diagram Conventions

Throughout these documents:

```
┌──────────┐  ┌──────────┐
│  Class   │  │ Interface│     — Box = class or interface
└────┬─────┘  └──────────┘
     │
     ▼
┌──────────┐
│ Subsystem│                     — Arrow = depends on / calls
└──────────┘

──>  synchronous call
~~>  asynchronous / streaming
- - > event emission
```

## Re-implementation Roadmap (Recommended Order)

| Phase | Modules | Why this order |
|-------|---------|----------------|
| **Phase 1: Foundation** | Kaos (02) → Config (04) → Kosong (03) | Kaos is needed for file I/O; Config is needed to connect providers; Kosong providers need only HTTP |
| **Phase 2: Agent Core** | Loop (08) → Agent (05) → Context (12) → Turn (06) | Loop is pure function, easiest to port; then wire into Agent; Context enables actual conversations; Turn bridges user input to loop |
| **Phase 3: Services** | Records (14) → Session (07) → Tools (09) → Permission (11) → Hooks (11) | Records enables persistence; Session enables multi-agent; Tools give the agent actions; Permission + Hooks control safety |
| **Phase 4: Extensions** | Skills (13) → MCP (10) | Skills are file-based, easy; MCP requires an external protocol implementation |
| **Phase 5: Application** | CLI/TUI (15) → SDK (16) | Most platform-specific, port last |

---

**Legend**: Throughout these docs, file paths are relative to the monorepo root unless prefixed with `packages/` or `apps/`.
