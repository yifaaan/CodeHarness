# CodeHarness

**Map, not encyclopedia.** For deep dives, follow the links.

## Implemented Modules

| Module | Namespace | Directory | Status |
|--------|-----------|-----------|--------|
| **Host** | `codeharness::host` | `src/codeharness/host/` | ✅ Implemented |
| **Llm** | `codeharness::llm` | `src/codeharness/llm/` | ✅ Implemented |
| **Config** | `codeharness::config` | `src/codeharness/config/` | ✅ Implemented |
| **Engine** | `codeharness::engine` | `src/codeharness/engine/` | ✅ Implemented |
| **Tools** | `codeharness::tools` | `src/codeharness/tools/` | ✅ Implemented |

### Host (Execution Environment Abstraction)

The Host layer abstracts filesystem and process operations. See [src/codeharness/host/](src/codeharness/host/) for source.

**Key interfaces:**
- `Host` — abstract base (path, file, process operations)
- `HostProcess` — running process handle
- `HostPath` — pathlib-like path utility
- `LocalHost` — local machine implementation

**All functions use PascalCase** (e.g. `ReadText`, `GetCwd`, `ExecWithEnv`). See [docs/coding-conventions.md](docs/coding-conventions.md).

### Llm (LLM Provider Abstraction)

The Llm layer provides a unified interface for LLM providers with streaming support. See [src/codeharness/llm/](src/codeharness/llm/) for source.

**Key interfaces:**
- `ChatProvider` — abstract base (`Generate` with callback-based streaming)
- `HttpClient` — abstract HTTP client (testable via dependency injection)
- `BeastHttpClient` — boost-beast + OpenSSL HTTPS implementation
- `OpenAiProvider` — OpenAI Chat Completions provider (OpenAI-compatible APIs)
- `SseParser` — Server-Sent Events line/event parser

**Design:** Provider receives `HttpClient*` (abstract). Tests use `MockHttpClient` with canned SSE responses — no network needed. Streaming uses callback-based model: `on_text`, `on_tool_call_start/delta`, `on_finish`.

### Config (Configuration + Provider Management)

The Config layer loads `config.toml`, validates it, and resolves model aliases to live `ChatProvider` instances. See [src/codeharness/config/](src/codeharness/config/) for source.

**Key interfaces:**
- `KimiConfig` — root config struct (`defaultModel`, `providers`, `models`, `thinking`)
- `ConfigManager` — TOML load/save/validate via injected `Host*` (`~/.codeharness/config.toml`, `$CODEHARNESS_HOME` override)
- `ProviderManager` — factory: model alias → `[providers]` config → credentials → `OpenAiProvider`
- `ProviderType` / `PermissionMode` / `ResolvedProviderConfig` — enums + static capability view

**Design:** All file I/O goes through `Host*` (testable, no direct disk). TOML parsed with `toml++`. Credentials resolve from `api_key` field → `[providers.<n>.env]` sub-table → (OpenAiProvider's own `OPENAI_API_KEY` fallback); `api_key` values support `$VAR`/`${VAR}` expansion against the process environment. Only the OpenAI-compatible family (`openai`, `kimi`, `openai_responses`) is constructible today; `anthropic`/`google-genai`/`vertexai` parse and validate but return `UnimplementedError`. OAuth, permission-rule evaluation, and hooks are deferred.

### Engine (Turn Execution Loop)

The Engine module implements the stateless turn execution loop — the agent's "brain". See [src/codeharness/engine/](src/codeharness/engine/) for source.

**Key interfaces:**
- `ExecutableTool` — abstract two-phase tool interface (`ResolveExecution` → `Execute`)
- `RunTurn(TurnInput)` — stateless loop function (LLM → tools → repeat)
- `LoopEvent` — variant of all event types (StepStarted, AssistantDelta, ToolResult, etc.)
- `LoopHooks` — optional extension points (beforeStep, afterStep, shouldContinueAfterStop)

**Design:** `RunTurn` is fully stateless — all dependencies injected via `TurnInput`. The loop calls `ChatProvider::Generate`, accumulates tool calls, executes them via `ExecutableTool`, and repeats until completion. Tests use `MockChatProvider` + `MockTool` — no real LLM or network needed.

### Tools (Built-in Actions)

The Tools module implements concrete `ExecutableTool` subclasses that let the agent act on the world. See [src/codeharness/tools/](src/codeharness/tools/) for source.

**Key interfaces:**
- `ToolManager` — owns tool instances; `Register`, `Find(name)`, `LoopTools()`
- `ReadFileTool`, `WriteFileTool`, `EditFileTool` — file I/O (Host-backed)
- `GlobTool`, `GrepTool` — file search (Grep uses in-process `re2`, no external binary)
- `BashTool` — shell execution with timeout + cancellation (two-phase kill)
- `TruncateOutput` / `NumberLines` / `SplitLines` — output helpers (`tools/tool_output.h`)

**Design:** Each tool implements the two-phase `ResolveExecution` (pure validation, sets `requires_permission`) → `Execute` (side effects via `Host`). All I/O goes through `codeharness::host::Host`. Read-only tools (Read/Glob/Grep) are auto-allow; Write/Edit/Bash require permission. Bash drains stdout/stderr deadlock-free via `HostProcess::Drain` (reproc poll, single-threaded). Active-set selection, MCP, and user-defined tools are deferred to the (future) Agent layer.

**Engine host wiring:** `TurnInput.host` (`host::Host*`) is propagated into each tool's `ToolContext` by `RunTurn`.

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
| `boost-beast` + `OpenSSL` | HTTPS client (streaming SSE) |
| `nlohmann-json` | JSON serialization |
| `toml++` | Config file (`config.toml`) parsing |
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

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, invoke the `skill` tool with `skill: "graphify"` before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
