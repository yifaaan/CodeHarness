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
| **Agent** | `codeharness::agent` | `src/codeharness/agent/` | ✅ Implemented |
| **Records** | `codeharness::records` | `src/codeharness/records/` | ✅ Implemented |
| **Permission** | `codeharness::permission` | `Source/CodeHarness/Permission/` | ✅ Implemented (MVP) |
| **Session** | `codeharness::session` | `Source/CodeHarness/Session/` | ✅ Implemented (MVP) |
| **Cli** | `codeharness::cli` | `Source/CodeHarness/Cli/` | ✅ Implemented (MVP) |
| **Context** | `codeharness::context` | `Source/CodeHarness/Context/` | ✅ Implemented (MVP) |
| **Tui** | `codeharness::tui` | `Source/CodeHarness/Tui/` | ✅ Implemented (FTXUI) |

### Host (Execution Environment Abstraction)

The Host layer abstracts filesystem and process operations. See [src/codeharness/host/](src/codeharness/host/) for source.

**Key interfaces:**
- `Host` — abstract base (path, file, process operations)
- `HostProcess` — running process handle
- `HostPath` — pathlib-like path utility
- `LocalHost` — local machine implementation

**All functions use PascalCase** (e.g. `ReadText`, `GetCwd`, `ExecWithEnv`, `AppendText`). See [docs/coding-conventions.md](docs/coding-conventions.md).

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

**Design:** All file I/O goes through `Host*` (testable, no direct disk). TOML parsed with `toml++`. Credentials resolve from `api_key` field → `[providers.<n>.env]` sub-table → (OpenAiProvider's own `OPENAI_API_KEY` fallback); `api_key` values support `$VAR`/`${VAR}` expansion against the process environment. Only the OpenAI-compatible family (`openai`, `kimi`, `openai_responses`) is constructible today; `anthropic`/`google-genai`/`vertexai` parse and validate but return `UnimplementedError`. The `PermissionMode` enum (`Manual`/`Auto`/`Yolo`) defined here is parsed from `default_permission_mode` and now consumed by the Permission module. OAuth, the permission rules DSL, and hooks remain deferred.

### Engine (Turn Execution Loop)

The Engine module implements the stateless turn execution loop — the agent's "brain". See [src/codeharness/engine/](src/codeharness/engine/) for source.

**Key interfaces:**
- `ExecutableTool` — abstract two-phase tool interface (`ResolveExecution` → `Execute`)
- `RunTurn(TurnInput)` — stateless loop function (LLM → tools → repeat)
- `LoopEvent` — variant of all event types (StepStarted, AssistantDelta, ToolResult, etc.)
- `LoopHooks` — optional extension points (beforeStep, afterStep, shouldContinueAfterStop)

**Design:** `RunTurn` is fully stateless — all dependencies injected via `TurnInput`. The loop calls `ChatProvider::Generate`, accumulates tool calls, executes them via `ExecutableTool`, and repeats until completion. The permission gate is consulted between `ResolveExecution` and `Execute` (see Permission module); a null gate means allow-all (back-compat). Tests use `MockChatProvider` + `MockTool` — no real LLM or network needed.

### Tools (Built-in Actions)

The Tools module implements concrete `ExecutableTool` subclasses that let the agent act on the world. See [src/codeharness/tools/](src/codeharness/tools/) for source.

**Key interfaces:**
- `ToolManager` — owns tool instances; `Register`, `Find(name)`, `LoopTools()`
- `ReadFileTool`, `WriteFileTool`, `EditFileTool` — file I/O (Host-backed)
- `GlobTool`, `GrepTool` — file search (Grep uses in-process `re2`, no external binary)
- `BashTool` — shell execution with timeout + cancellation (two-phase kill)
- `TruncateOutput` / `NumberLines` / `SplitLines` — output helpers (`tools/tool_output.h`)

**Design:** Each tool implements the two-phase `ResolveExecution` (pure validation, sets `requiresPermission`) → `Execute` (side effects via `Host`). All I/O goes through `codeharness::host::Host`. Read-only tools (Read/Glob/Grep) set `requiresPermission = false` and always run; Write/Edit/Bash set `requiresPermission = true` and are now gated by the Permission module's `PermissionGate` inside `ExecuteToolCall`. Bash drains stdout/stderr deadlock-free via `HostProcess::Drain` (reproc poll, single-threaded). Active-set selection, MCP, and user-defined tools are deferred to the (future) Agent layer.

**Engine host wiring:** `TurnInput.host` (`host::Host*`) is propagated into each tool's `ToolContext` by `RunTurn`.

### Agent (Composition Root v1)

The Agent layer provides the first user-facing composition layer over the stateless Engine loop. See [Source/CodeHarness/Agent/](Source/CodeHarness/Agent/) for source.

**Key interfaces:**
- `Agent` — synchronous prompt API (`Prompt`, `Cancel`, `ClearContext`, active tool filtering)
- `AgentConfig` / `AgentProfile` — system prompt, max steps, default active tools
- `AgentEvent` — turn lifecycle, status changes, errors, and forwarded loop events
- `PromptResult` — turn id, stop reason, usage, and error summary

**Design:** `Agent` receives non-owning `ChatProvider*`, `Host*`, and `ToolManager*` dependencies. It keeps in-memory conversation history as a `ContextMemory` (so the context window can be budgeted), converts prompt text into `llm::Message`, calls `engine::RunTurn`, forwards loop events, and stores `TurnResult.updatedHistory` back into the `ContextMemory`. Before each turn it may run between-turn compaction (see Context module) when history exceeds the model's window. Persistence is wired via `SetRecords(records::AgentRecords*)` (best-effort logging of turn lifecycle + loop events); `Resume()` replays the wire stream into the `ContextMemory` (used by the Session module on resume). Permission gating is opt-in via `SetPermissionMode(config::PermissionMode)` + `SetApprovalCallback(...)`; the Agent then owns a `PermissionGate` threaded into each `TurnInput`. Hooks, skills, and MCP remain deferred modules.

### Records (Event Sourcing)

The Records module implements append-only event sourcing over a Host-backed `wire.jsonl` stream. See [Source/CodeHarness/Records/](Source/CodeHarness/Records/) for source.

**Key interfaces:**
- `AgentRecords` — owns a `RecordPersistence`; `Log(record)`, `ReadAll()`, `Replay(apply)`, `IsRestoring()`, `Flush()`, `Close()`
- `RecordPersistence` — abstract append/read/flush/close backend
- `FilePersistence` — `Host`-backed `wire.jsonl` (append via `Host::AppendText`, read via `Host::ReadLines`)
- `RecordTypes.h` — `AgentRecord` variant + 4 record kinds (minimal set): `TurnPromptRecord`, `TurnCancelRecord`, `ContextAppendMessageRecord`, `ContextAppendLoopEventRecord`
- `RecordJson.h` — `WireRecordToJson` / `ParseWireRecord` + per-payload helpers (`MessageToJson`, `LoopEventToJson`, `ContentPartToJson`)

**Design:** All I/O goes through `Host*` (testable, no direct disk). The minimal 4-kind set covers `turn.prompt`, `turn.cancel`, `context.append_message`, `context.append_loop_event`. Wire format is one JSON object per line with `{"type","...","ts":<ms>,"protocol":"1.0",...}`. `AgentRecords::Replay` toggles `restoring_` for the duration of the apply loop; `Log()` short-circuits to `OkStatus` when `restoring_` is set, so replay never re-records. **Session now owns the directory layout** (`<root>/<workdir-key>/<sessionId>/agents/<agentId>/wire.jsonl`); Records constructs `FilePersistence` at a path computed by the Session layer. The `LoopEventToJson`/`LoopEventFromJson` helpers now cover `PermissionRequestedEvent` and `PermissionDeniedEvent` so the new loop events round-trip through the wire stream. Additional record kinds (compaction/plan_mode/tools/usage) deferred until the corresponding modules come online. `Host::AppendText` enables atomic append semantics without read-rewrite.

### Permission (Tool Approval Gate)

The Permission module gates tool execution — the single consumer of `ToolExecution::requiresPermission` in the loop. See [Source/CodeHarness/Permission/](Source/CodeHarness/Permission/) for source.

**Key interfaces:**
- `PermissionGate` — decides whether a resolved tool may run; constructed with a `config::PermissionMode` + `ApprovalCallback`
- `PermissionTypes.h` — `PermissionDecision` (`Allow`/`Deny`) and the `ApprovalCallback` signature
- `PermissionRequestedEvent` / `PermissionDeniedEvent` — new `LoopEvent` kinds dispatched around the approval flow

**Design (MVP scope):** Two effective modes. **Yolo** short-circuits and allows every tool without invoking the callback. **Manual** allows read-only tools (`requiresPermission == false`) automatically and invokes the `ApprovalCallback` for mutating tools, running them only on `Allow`. **Auto** is parsed from config but not yet implemented — it falls back to Manual behavior with a one-shot warning (true session-scoped Auto needs the Session module). A missing callback in Manual mode denies mutating tools (safe default) until a UI wires a real approval flow. The gate holds no per-session state and is owned by the Agent (`SetPermissionMode`/`SetApprovalCallback`). Out of scope (deferred to plan #11): the permission rules DSL, `PermissionRule`/`Policy` types, real Auto mode, audit logging of decisions, and the full 13-event HookEngine. Closes tech-debt TD-003.

### Session (Persistence + Lifecycle)

The Session module owns the on-disk session layout and ties a directory to a live Agent + its Records sink. See [Source/CodeHarness/Session/](Source/CodeHarness/Session/) for source.

**Key interfaces:**
- `SessionStore` — directory-layout owner: `Create`/`Get`/`List`/`Find`/`Remove`/`RenameTitle`, plus `ReadMeta`/`WriteMeta` (atomic `state.json` via `Host::Rename`) and `AppendIndex` (`session_index.jsonl`)
- `Session` — lifecycle facade: `Create(store, cfg)` / `Resume(store, cfg, sessionId)` / `Close()`; owns the `'main'` `Agent` + its `AgentRecords`
- `SessionConfig` / `SessionMeta` / `SessionDir` / `SessionInfo` — value types
- `EncodeWorkdirKey(absoluteWorkdir)` — sanitized-prefix + FNV-1a-64-hex path encoder (no new dependency)

**Design (MVP scope):** Directory layout `<root>/<workdir-key>/<sessionId>/{state.json, agents/<agentId>/wire.jsonl}` with `<root>` resolved exactly like config: `$CODEHARNESS_HOME/sessions` if set, else `$HOME/.codeharness/sessions`. `Session::Create` allocates the dir via the store, then wires `FilePersistence(host, <dir>/agents/main/wire.jsonl)` → `AgentRecords` → `Agent`, calling `Agent::SetRecords`. `Session::Resume` wires the same then calls `Agent::Resume()` which replays the wire stream into in-memory history (Records' `restoring_` guard prevents re-recording). `Close()` flushes records and writes updated `state.json` (atomic). `SessionStore::WriteMeta` is atomic via write-tmp + `Host::Rename`. Out of scope (deferred to plan #09): the RPC protocol (CoreAPI/AgentAPI), `fork`/`export`, subagents (only `'main'` this iteration), and skill/MCP/hook ownership.

**Host additions:** `Host::Remove` (`RemoveOptions{recursive, existOk}`) and `Host::Rename(from, to)` were added to the `Host` interface (and implemented on `LocalHost`) to unblock `SessionStore::Remove` and atomic `state.json` writes — these methods were previously absent.

### Cli (Non-Interactive Entry Point)

The Cli module is the first executable target — turns the library into a runnable program. See [Source/CodeHarness/Cli/](Source/CodeHarness/Cli/) for source.

**Key interfaces:**
- `ParseArgs(argc, argv)` → `StatusOr<CliOptions>` — CLI11-based flag parser
- `CliOptions` — `{prompt, model, workdir, yolo, help, version, mode (Prompt|Shell|Tui), sessionId, continueLast}`
- `Run(opts, RunDeps)` → `Status` — the end-to-end wiring chain for prompt/shell modes
- `RunDeps` — test seam: `{host*, http*, resolveProvider}`, letting tests inject a mock provider without touching the network
- `ResolveProviderFromConfig(host, http, modelOverride)` — ConfigManager → ProviderManager → live `ChatProvider`

**Design:** `Run` handles both one-shot `--prompt` and interactive `shell` modes. Both load `config.toml`, resolve the provider, register tools, and wire callbacks. `Main.cpp` additionally routes `CliMode::Tui` to `tui::Run(host, http, opts)` before falling through to the shell path. The shell mode supports `--session ID` resume, `--continue` (latest session), `/help` `/clear` `/skill` slash commands. The `tui` subcommand accepts the same flags (`--session`, `--continue`, `-y`, `-m`, `--workdir`) and dispatches to the full-screen FTXUI app (see Tui module).

**Build:** `cmake --build` produces `codeharness_cli.exe` alongside `codeharness_tests.exe`. `CliParser.cpp`/`RunPrompt.cpp` live in the `codeharness` library (so tests link them); only `Main.cpp` is exclusive to the executable. CLI11 is now a library dependency.

### Context (Memory + Compaction)

The Context module stops `Agent::history` from growing unbounded. See [Source/CodeHarness/Context/](Source/CodeHarness/Context/) for source.

**Key interfaces:**
- `ContextMemory` — owns the conversation `std::vector<llm::Message>` plus a cached running token estimate; `Append`/`ReplaceAll`/`Clear`/`Messages`/`TokenCount`
- `TokenEstimate` — heuristic `EstimateTokens(text|Message|span)` ≈ chars/4 + per-message/tool-call overhead (conservative; swap point for a real tokenizer)
- `Compactor` — `ShouldCompact(usedTokens, cfg)` + `Compact(provider, history, cfg)` (summarizes the prefix via a second `Generate`, keeps the tail); `BuildCompactedHistory` builds the summary + retained tail
- `CompactionConfig` — `{maxContextTokens, compactThreshold=0.75, retainTail=10}`; `ContextCompactingEvent` in the `AgentEvent` variant

**Design (MVP scope):** Between-turn compaction only, living entirely in the Agent layer. `Agent` now holds a `ContextMemory` instead of a bare vector. In `Prompt()`, before building the turn history, it resolves `maxContextTokens` from `llm::GetCapability(provider->ModelName())` (unless `SetCompactionConfig` overrode it), and if `history.tokenCount() + incoming` crosses 75% it runs `Compact` — a second `Generate` produces a summary, the prefix is replaced by one summary message, the last 10 messages are kept verbatim. **Zero changes to `Loop.cpp`, `LoopTypes.h`, `LoopEvent`, `ChatProvider`, or any provider** — the loop keeps receiving a plain (shorter) `std::vector<llm::Message>`. `Resume()` replays records through `ContextMemory::Append` to keep the token cache consistent. Closes tech-debt TD-006. Out of scope (deferred to plan #06): the `InjectionManager` (plan/permission-mode injection — needs a wider `LoopHooks::beforeStep`), mid-turn compaction (between tool-call steps inside one turn), a real `CountTokens` provider virtual, and `ContextMessage` metadata (origin/id/createdAt).

**Llm helper exported:** `ConcatTextParts` (flattens a message's content parts into one string) was promoted from `MessageJson.cpp`'s anonymous namespace to a public `codeharness::llm` function in `MessageJson.h`, so `TokenEstimate` can measure message text.

### Tui (Full-Screen Terminal UI)

The Tui module is the full-screen interactive UI built on FTXUI. See [Source/CodeHarness/Tui/](Source/CodeHarness/Tui/) for source. It's a port of the Kimi Code TUI design (a TypeScript app using pi-tui) into modern C++20.

**Key interfaces:**
- `TuiApp` — top-level orchestrator owning the FTXUI screen + component tree; runs `screen->Loop(component)` on the UI thread and detaches a worker thread per `Prompt` call
- `TuiState` — mutex-protected shared state (transcript entries, tool call tracking, pending approval/question promises, theme palette, modal kind); read by the UI render loop, mutated by the EventRouter
- `EventRouter` — converts `rpc::CoreEvent` → `TuiState` mutations (one method per event type: `OnAssistantDelta`, `OnToolCallStarted`, `OnToolResult`, `OnPermissionRequested`, `OnTurnEnded`, etc.)
- `MarkdownRenderer` — line-based markdown parser → FTXUI Element (supports headings, bold/italic/inline code, fenced code blocks, lists, blockquotes, horizontal rules)
- `Run(host, http, opts)` — top-level entry point called by `Cli/Main.cpp` when `opts.mode == CliMode::Tui`

**Design (threading model):**
1. UI thread runs `ScreenInteractive::Loop` — owns FTXUI component tree, processes keyboard input, builds Element tree on each render
2. Worker thread runs `CoreApi::Prompt` (a blocking call inside `std::thread([this, text]{ api->Prompt(...); }).detach()`)
3. `eventSink` callback fires on the worker thread → `EventRouter::Dispatch` mutates `TuiState` under mutex → `screen->PostEvent(Event::Custom)` triggers a re-render on the UI thread
4. `ApprovalCallback` / `QuestionCallback` are synchronous (block the worker thread) → push a `std::promise` into `TuiState`, wake the UI via PostEvent, then `future.wait_for(5min)` blocks until the user picks a button (or auto-deny on timeout/Escape)

**Visual design (port of Kimi):**
- Status icons: spinner (running), `+` (done), `x` (error) with semantic colors (yellow/green/red)
- Tool call cards: header line (icon + name + args preview from `path/command/pattern/...`), body shown after completion
- Modal overlays via `Container::Stacked` + `Maybe` guards checking `state->activeModal`
- Approval panel: bordered double frame with `A`/`D` keyboard shortcuts
- Question dialog: `Radiobox` for fixed options + `Input` for free-form
- Slash commands: `/help` `/clear` `/exit` `/model [name]` `/sessions` `/mode`

**Build:** `codeharness_tui` is a separate library linked to `codeharness_cli`. Tests link both `codeharness` + `codeharness_tui`. FTXUI is found via `find_package(ftxui CONFIG REQUIRED)` (already in `vcpkg.json`).

**Launch:** `codeharness tui [--session ID] [--continue] [-y] [-m model] [--workdir DIR]`. Reuses the same `config.toml` and provider resolution as the shell/CLI modes.

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