# Kimi Code CLI

AI coding agent that runs in the terminal. Reads and edits code, executes shell commands, searches files and the web, and autonomously plans and adjusts its next actions based on feedback.

## Quick Start

```bash
# Start interactive TUI
kimi

# Single prompt mode
kimi -p "explain this codebase"

# Resume session
kimi -C
```

## Codebase Map

This is a **map**, not an encyclopedia. For deep dives, follow the links.

```
docs/plan/re-build/
├── AGENTS.md              # This file — entry point (you are here)
├── ARCHITECTURE.md        # High-level architecture overview
├── design-docs/           # Design principles and system overview
│   ├── core-beliefs.md    # Core design principles
│   ├── system-overview.md # System boundaries and data flow
│   └── data-flow.md       # Detailed data flow diagrams
├── exec-plans/            # Execution plans and roadmaps
│   ├── active/            # Current work in progress
│   └── completed/         # Finished plans
└── references/            # Module reference documentation
    ├── kaos-interface.md      # Filesystem/process abstraction
    ├── kosong-interface.md    # LLM provider abstraction
    ├── config-schema.md       # Configuration system
    ├── agent-lifecycle.md     # Agent/session/RPC lifecycle
    ├── loop-engine.md         # Turn execution loop
    ├── tool-system.md         # Built-in tools
    ├── mcp-integration.md     # MCP client integration
    ├── permission-hooks.md    # Permission and hook system
    ├── context-compaction.md  # Context memory and compaction
    ├── skill-system.md        # Skill/slash command system
    ├── records-replay.md      # Event sourcing and replay
    ├── tui-layer.md           # CLI and TUI layer
    └── sdk-interface.md       # Public SDK
```

## Core Principles

1. **Event Sourcing**: All state changes recorded as append-only events in `wire.jsonl`. Enables crash recovery and session replay.

2. **Two-Phase Tool Execution**: Tools have `resolveExecution()` (pure validation) and `execute()` (side effects). Enables permission checking before I/O.

3. **Stateless Loop**: The `runTurn()` function has no hidden state — all dependencies injected. Testable and portable.

4. **Progressive Disclosure**: Documentation organized from high-level (this file) → architecture → design docs → module references.

## Key Subsystems

| Subsystem | Purpose | Key File |
|-----------|---------|----------|
| **Kaos** | Filesystem/process abstraction | `references/kaos-interface.md` |
| **Kosong** | LLM provider unification | `references/kosong-interface.md` |
| **Agent** | Core orchestrator | `references/agent-lifecycle.md` |
| **Loop** | Turn execution engine | `references/loop-engine.md` |
| **Tools** | Action primitives | `references/tool-system.md` |
| **Context** | Conversation memory | `references/context-compaction.md` |
| **Records** | Event sourcing | `references/records-replay.md` |

## Technology Stack

- **Runtime**: Node.js ≥ 24.15.0
- **Language**: TypeScript 6.0.2 (strict mode, ESM)
- **Package Manager**: pnpm 10+ (workspace monorepo)
- **TUI Framework**: `@earendil-works/pi-tui`
- **LLM SDKs**: Anthropic, OpenAI, Google GenAI, Moonshot

## Design Docs

- [Core Beliefs](design-docs/core-beliefs.md) — Design principles and philosophy
- [System Overview](design-docs/system-overview.md) — System boundaries and components
- [Data Flow](design-docs/data-flow.md) — Request/response flow diagrams

## Reference Docs

See [references/index.md](references/index.md) for the complete module reference.

## Contributing

When modifying code:
1. Check [ARCHITECTURE.md](ARCHITECTURE.md) for system boundaries
2. Read relevant reference doc for the module you're touching
3. Follow existing patterns in the codebase
4. Update documentation if architecture changes
5. Ensure all linters and tests pass
