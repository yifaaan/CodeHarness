# CodeHarness

**Map, not encyclopedia.** For deep dives, follow the links.

## Implemented Modules

| Module | Namespace | Directory | Status |
|--------|-----------|-----------|--------|
| **Host** | `codeharness::host` | `src/codeharness/host/` | ✅ Implemented |
| **Llm** | `codeharness::llm` | `src/codeharness/llm/` | 📋 Planned |

### Host (Execution Environment Abstraction)

The Host layer abstracts filesystem and process operations. See [src/codeharness/host/](src/codeharness/host/) for source.

**Key interfaces:**
- `Host` — abstract base (path, file, process operations)
- `HostProcess` — running process handle
- `HostPath` — pathlib-like path utility
- `LocalHost` — local machine implementation

**All functions use PascalCase** (e.g. `ReadText`, `GetCwd`, `ExecWithEnv`). See [docs/coding-conventions.md](docs/coding-conventions.md).

## Core Principles

1. **Event Sourcing**: All state changes recorded as append-only events
2. **Two-Phase Tool Execution**: validate → execute
3. **Stateless Loop**: `run_query` has no hidden state
4. **Progressive Disclosure**: This file → architecture → design docs → code

## Library Usage

| Library | Purpose |
|---------|---------|
| `absl::Status` / `absl::StatusOr` | Error handling (no custom exceptions) |
| `p-ranav-glob` (`glob::glob`, `glob::rglob`) | File globbing |
| `fmt` | String formatting |
| `spdlog` | Logging |
| `reproc++` | Process spawning |
| `doctest` | Unit testing |

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
