# References Index

Module reference documentation for Kimi Code CLI.

## Infrastructure Layer

| Module | Description | Doc |
|--------|-------------|-----|
| **Kaos** | Filesystem and process execution abstraction | [kaos-interface.md](kaos-interface.md) |
| **Kosong** | LLM provider abstraction | [kosong-interface.md](kosong-interface.md) |
| **Config** | Configuration schema and provider management | [config-schema.md](config-schema.md) |

## Agent Core

| Module | Description | Doc |
|--------|-------------|-----|
| **Agent Lifecycle** | Agent, Session, RPC, TurnFlow | [agent-lifecycle.md](agent-lifecycle.md) |
| **Loop Engine** | Turn execution loop | [loop-engine.md](loop-engine.md) |
| **Tool System** | Built-in tools and tool management | [tool-system.md](tool-system.md) |
| **Context & Compaction** | Conversation memory and compaction | [context-compaction.md](context-compaction.md) |
| **Records & Replay** | Event sourcing and session replay | [records-replay.md](records-replay.md) |

## Extensions

| Module | Description | Doc |
|--------|-------------|-----|
| **MCP Integration** | Model Context Protocol client | [mcp-integration.md](mcp-integration.md) |
| **Permission & Hooks** | Permission rules and event hooks | [permission-hooks.md](permission-hooks.md) |
| **Skill System** | Skills and slash commands | [skill-system.md](skill-system.md) |

## Application Layer

| Module | Description | Doc |
|--------|-------------|-----|
| **TUI Layer** | CLI and terminal UI | [tui-layer.md](tui-layer.md) |
| **SDK Interface** | Public TypeScript SDK | [sdk-interface.md](sdk-interface.md) |

## Dependency Graph

```
CLI/TUI ──> SDK ──> Session ──> Agent ──> Loop ──> Kosong
                     │            │         │         │
                     │            │         │         ▼
                     │            │         │      HTTP APIs
                     │            │         │
                     │            │         ▼
                     │            │      Kaos (I/O)
                     │            │
                     │            ▼
                     │         Tools ──> Kaos
                     │         MCP
                     │         Permission
                     │         Hooks
                     │         Context
                     │         Compaction
                     │         Skills
                     │         Records
                     │
                     ▼
                  SessionStore
```

## Quick Reference

### Core Interfaces

| Interface | Location | Purpose |
|-----------|----------|---------|
| `Kaos` | [kaos-interface.md](kaos-interface.md) | Filesystem/process abstraction |
| `ChatProvider` | [kosong-interface.md](kosong-interface.md) | LLM provider interface |
| `ExecutableTool` | [tool-system.md](tool-system.md) | Tool interface |
| `AgentAPI` | [agent-lifecycle.md](agent-lifecycle.md) | Agent RPC methods |
| `Session` | [agent-lifecycle.md](agent-lifecycle.md) | Session management |

### Key Classes

| Class | Module | Purpose |
|-------|--------|---------|
| `Agent` | [agent-lifecycle.md](agent-lifecycle.md) | Core orchestrator |
| `TurnFlow` | [agent-lifecycle.md](agent-lifecycle.md) | Turn lifecycle |
| `ContextMemory` | [context-compaction.md](context-compaction.md) | Conversation history |
| `AgentRecords` | [records-replay.md](records-replay.md) | Event sourcing |
| `ToolManager` | [tool-system.md](tool-system.md) | Tool registration |

## See Also

- [../ARCHITECTURE.md](../ARCHITECTURE.md) — High-level architecture
- [../design-docs/core-beliefs.md](../design-docs/core-beliefs.md) — Design principles
- [../design-docs/system-overview.md](../design-docs/system-overview.md) — System boundaries
