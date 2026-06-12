# CodeHarness

**Map, not encyclopedia.** For deep dives, follow the links.

## Quick Start

```bash
# Build (Windows)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug

# Build (Linux)
cmake --preset linux-debug
cmake --build --preset linux-debug

# Test
ctest --preset windows-msvc-debug  # or linux-debug
```

## Codebase Map

```
CodeHarness/
├── AGENTS.md              # This file — entry point (you are here)
├── ARCHITECTURE.md        # High-level architecture overview
├── docs/
│   ├── kimi-code-analysis/    # Kimi Code architecture analysis
│   │   ├── INDEX.md           # Analysis index
│   │   ├── architecture-overview.md
│   │   ├── core-components.md
│   │   ├── design-patterns.md
│   │   └── implementation-guide.md
│   ├── plan/
│   │   └── re-build/          # Re-implementation design docs
│   │       ├── AGENTS.md      # Kimi Code entry point
│   │       ├── ARCHITECTURE.md
│   │       ├── design-docs/
│   │       ├── exec-plans/
│   │       └── references/
│   └── ...
└── src/codeharness/           # Source code
    ├── core/                  # Foundation
    ├── engine/                # Agent engine
    ├── tools/                 # Tool system
    ├── permissions/           # Permission system
    ├── hooks/                 # Hook system
    └── ...
```

## Core Principles

1. **Event Sourcing**: All state changes recorded as append-only events
2. **Two-Phase Tool Execution**: validate → execute
3. **Stateless Loop**: `run_query` has no hidden state
4. **Progressive Disclosure**: This file → architecture → design docs → code

## Key Subsystems

| Subsystem | Purpose | Key File |
|-----------|---------|----------|
| **Engine** | Core agent loop | `src/codeharness/engine/` |
| **Tools** | Action primitives | `src/codeharness/tools/` |
| **Permissions** | Access control | `src/codeharness/permissions/` |
| **Hooks** | Extension points | `src/codeharness/hooks/` |
| **Context** | Conversation memory | `src/codeharness/memory/` |

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for:
- System boundaries and data flow
- Package dependency graph
- Key design decisions

## Re-implementation

See [docs/plan/re-build/](docs/plan/re-build/) for:
- Kimi Code architecture analysis
- Module reference documentation
- Execution plans

## Design Principles

1. **Avoid reinventing the wheel**: Use stable third-party libraries
2. **Keep code clear and direct**: Prefer simple structs + free functions
3. **Moderate safety**: Permission checks before tool execution
4. **Unified message model**: All providers convert to same internal model
5. **Event-driven**: Engine emits events, UI consumes them
6. **Tool failure does not crash**: Convert to `ToolResultBlock{is_error=true}`

## Build & Test

Requires [vcpkg](https://vcpkg.io/) with `VCPKG_ROOT` set.

```bash
# Windows (MSVC)
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# Linux
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Test framework: [doctest](https://github.com/doctest/doctest). Tests in `tests/` as `*_tests.cpp`.

## Coding Conventions

See [docs/coding-conventions.md](docs/coding-conventions.md) for:
- C++ Core Guidelines compliance
- RAII, immutability, type safety
- Naming conventions (snake_case, PascalCase)
- Header and source rules
- Resource and error handling

## Quality Metrics

| Metric | Target | Current |
|--------|--------|---------|
| Test Coverage | > 80% | TBD |
| Documentation Freshness | < 30 days | TBD |
| Linter Pass Rate | 100% | TBD |
| Build Success Rate | > 99% | TBD |

## Technical Debt

See [docs/plan/re-build/exec-plans/tech-debt-tracker.md](docs/plan/re-build/exec-plans/tech-debt-tracker.md) for tracking.

## Contributing

1. Read this file for navigation
2. Check [ARCHITECTURE.md](ARCHITECTURE.md) for system boundaries
3. Read relevant docs for the module you're touching
4. Follow existing patterns in the codebase
5. Ensure all linters and tests pass
6. Update documentation if architecture changes

---

**Source Layout**: `src/codeharness/` modules in CMake dependency order (top depends on bottom):

| Layer | Modules | CMake targets |
|-------|---------|---------------|
| CLI entry | `cli/` | `codeharness_cli` |
| Engine | `engine/` | `codeharness_engine` |
| Extensions | `skills/`, `plugins/`, `tools/` | `codeharness_extensions`, `codeharness_tools` |
| Context | `prompts/`, `memory/`, `commands/` | `codeharness_prompts`, `codeharness_memory`, `codeharness_commands` |
| Multi-agent | `tasks/`, `mailbox/` | `codeharness_tasks`, `codeharness_mailbox` |
| Infrastructure | `provider/`, `mcp/`, `permissions/`, `hooks/` | respective `codeharness_*` |
| Foundation | `core/` | `codeharness_foundation` |
