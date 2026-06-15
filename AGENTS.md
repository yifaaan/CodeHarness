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
| **Hooks** | `codeharness::hooks` | `Source/CodeHarness/Hooks/` | ✅ Implemented (MVP) |
| **Skills** | `codeharness::skills` | `Source/CodeHarness/Skills/` | ✅ Implemented |

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

**Design:** All file I/O goes through `Host*` (testable, no direct disk). TOML parsed with `toml++`. Credentials resolve from `api_key` field → `[providers.<n>.env]` sub-table → (OpenAiProvider's own `OPENAI_API_KEY` fallback); `api_key` values support `$VAR`/`${VAR}` expansion against the process environment. Only the OpenAI-compatible family (`openai`, `kimi`, `openai_responses`) is constructible today; `anthropic`/`google-genai`/`vertexai` parse and validate but return `UnimplementedError`. The `PermissionMode` enum (`Manual`/`Auto`/`Yolo`) defined here is parsed from `default_permission_mode` and consumed by the Permission module. The `[[hooks]]` array is parsed here into `std::vector<hooks::HookDef>` and consumed by the Hooks module. OAuth and the permission rules DSL remain deferred.

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

**Design:** `Agent` receives non-owning `ChatProvider*`, `Host*`, and `ToolManager*` dependencies. It keeps in-memory conversation history as a `ContextMemory` (so the context window can be budgeted), converts prompt text into `llm::Message`, calls `engine::RunTurn`, forwards loop events, and stores `TurnResult.updatedHistory` back into the `ContextMemory`. Before each turn it may run between-turn compaction (see Context module) when history exceeds the model's window. Persistence is wired via `SetRecords(records::AgentRecords*)` (best-effort logging of turn lifecycle + loop events); `Resume()` replays the wire stream into the `ContextMemory` (used by the Session module on resume). Permission gating is opt-in via `SetPermissionMode(config::PermissionMode)` + `SetApprovalCallback(...)`; the Agent then owns a `PermissionGate` threaded into each `TurnInput`. Hooks are opt-in via `SetHookEngine(...)`; the Agent fires its 3 events (UserPromptSubmit block + PreCompact/PostCompact) and threads the engine into each `TurnInput`. Skills are opt-in via `SetSkillManager(...)`; the Agent snapshots the base system prompt, installs two callbacks (inline skills → user message, prompt skills → staged system content), and rebuilds the per-turn system prompt as `base + skill catalog + staged prompt-skill content`, draining the stage each turn. MCP remains a deferred module.

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

**Design (MVP scope):** Two effective modes. **Yolo** short-circuits and allows every tool without invoking the callback. **Manual** allows read-only tools (`requiresPermission == false`) automatically and invokes the `ApprovalCallback` for mutating tools, running them only on `Allow`. **Auto** is parsed from config but not yet implemented — it falls back to Manual behavior with a one-shot warning (true session-scoped Auto needs the Session module). A missing callback in Manual mode denies mutating tools (safe default) until a UI wires a real approval flow. The gate holds no per-session state and is owned by the Agent (`SetPermissionMode`/`SetApprovalCallback`). Out of scope (deferred): the permission rules DSL, `PermissionRule`/`Policy` types, real Auto mode, and audit logging of decisions. Closes tech-debt TD-003.

### Session (Persistence + Lifecycle)

The Session module owns the on-disk session layout and ties a directory to a live Agent + its Records sink. See [Source/CodeHarness/Session/](Source/CodeHarness/Session/) for source.

**Key interfaces:**
- `SessionStore` — directory-layout owner: `Create`/`Get`/`List`/`Find`/`Remove`/`RenameTitle`, plus `ReadMeta`/`WriteMeta` (atomic `state.json` via `Host::Rename`) and `AppendIndex` (`session_index.jsonl`)
- `Session` — lifecycle facade: `Create(store, cfg)` / `Resume(store, cfg, sessionId)` / `Close()`; owns the `'main'` `Agent` + its `AgentRecords`
- `SessionConfig` / `SessionMeta` / `SessionDir` / `SessionInfo` — value types
- `EncodeWorkdirKey(absoluteWorkdir)` — sanitized-prefix + FNV-1a-64-hex path encoder (no new dependency)

**Design (MVP scope):** Directory layout `<root>/<workdir-key>/<sessionId>/{state.json, agents/<agentId>/wire.jsonl}` with `<root>` resolved exactly like config: `$CODEHARNESS_HOME/sessions` if set, else `$HOME/.codeharness/sessions`. `Session::Create` allocates the dir via the store, then wires `FilePersistence(host, <dir>/agents/main/wire.jsonl)` → `AgentRecords` → `Agent`, calling `Agent::SetRecords`. `Session::Resume` wires the same then calls `Agent::Resume()` which replays the wire stream into in-memory history (Records' `restoring_` guard prevents re-recording). `Close()` flushes records and writes updated `state.json` (atomic). `SessionStore::WriteMeta` is atomic via write-tmp + `Host::Rename`. Session fires the `SessionStart`/`SessionEnd` hooks when a `HookEngine` is supplied via `SessionConfig`. Out of scope (deferred to plan #09): the RPC protocol (CoreAPI/AgentAPI), `fork`/`export`, subagents (only `'main'` this iteration), and skill/MCP ownership.

**Host additions:** `Host::Remove` (`RemoveOptions{recursive, existOk}`) and `Host::Rename(from, to)` were added to the `Host` interface (and implemented on `LocalHost`) to unblock `SessionStore::Remove` and atomic `state.json` writes — these methods were previously absent.

### Cli (Non-Interactive Entry Point)

The Cli module is the first executable target — turns the library into a runnable program. See [Source/CodeHarness/Cli/](Source/CodeHarness/Cli/) for source.

**Key interfaces:**
- `ParseArgs(argc, argv)` → `StatusOr<CliOptions>` — CLI11-based flag parser
- `CliOptions` — `{prompt, model, workdir, yolo, help, version}`
- `Run(opts, RunDeps)` → `Status` — the end-to-end wiring chain
- `RunDeps` — test seam: `{host*, http*, resolveProvider}`, letting tests inject a mock provider without touching the network
- `ResolveProviderFromConfig(host, http, modelOverride)` — ConfigManager → ProviderManager → live `ChatProvider`

**Design (MVP scope):** One-shot `--prompt` mode only. `Run` wires the full chain: load `config.toml` → resolve provider → register built-in tools → `Session::Create` → wire the agent's event dispatcher (stream `AssistantDeltaEvent` text to stdout) → set permission mode (`Yolo` for `--yolo`, else `Manual` with a synchronous stdin y/n approval callback) → `Agent::Prompt` → `Session::Close`. `Main.cpp` owns the `LocalHost` + `BeastHttpClient` (synchronous, no threading) lifetimes and injects them via `RunDeps`. The `codeharness_cli` executable is always built (not gated on `CODEHARNESS_BUILD_TESTS`). Out of scope (deferred to plan #14): the TUI/`shell` mode, reverse-RPC approval panel, REPL/multi-turn, `--continue`/`--session` resume flags, `--output-format stream-json`, and a config auto-creation wizard.

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

### Hooks (Lifecycle Subprocess Hooks)

The Hooks module runs user-configured subprocess hooks on agent lifecycle events. See [Source/CodeHarness/Hooks/](Source/CodeHarness/Hooks/) for source.

**Key interfaces:**
- `HookEngine` — `Trigger(event, ctx)` (best-effort fan-out, 9 informational events) + `TriggerBlock(event, ctx)` (first blocker, 2 blocking events); constructed with a `std::vector<HookDef>` + non-owning `Host*`
- `HookTypes.h` — `HookEvent` (11 values), `HookAction` (`Allow`/`Block`), `HookDef` (one `[[hooks]]` entry), `HookResult`, `HookContext`
- `HookEventName` / `ParseHookEvent` — string ↔ enum round-trip used by config

**Design (MVP scope):** Command-type hooks only (no `callback` type). 11 of the 13 events from the reference design — the 2 subagent events (`SubagentStart`/`SubagentStop`) are deferred until subagents exist. Configured in `config.toml` as a `[[hooks]]` array (`event`, `command`, `matcher?`, `timeout?`). The engine spawns each hook via `Host::ExecWithEnv` (argv-style, no shell), pipes a JSON payload to stdin, drains stdout/stderr with the per-hook timeout, and inspects the exit code. **Fail-open invariant (Architecture Invariant #2):** a hook that fails (non-zero exit, timeout, crash) is ALWAYS treated as `Allow`. The only way a hook blocks is by printing a JSON line `{"action":"block","reason":"..."}` on stdout, and only for the 2 blocking events (`PreToolUse`, `UserPromptSubmit`). The `matcher` is a `std::regex` against the event target (tool name for tool events, session id for session events). `TurnInput.hookEngine` is an optional pointer (null = hooks disabled, back-compat). Out of scope (deferred): `callback`-type hooks, the 2 subagent events, the Permission rules DSL, and `InjectionManager`.

**Event wiring:** Loop fires 5 events (`PreToolUse` block + `PostToolUse`/`PostToolUseFailure`/`Stop`/`StopFailure`); Agent fires 3 (`UserPromptSubmit` block + `PreCompact`/`PostCompact`); Session fires 2 (`SessionStart`/`SessionEnd`); `Notification` is glue for callers. The Agent's hook engine is the shared instance within a session.

### Skills (Reusable Prompt/Workflow Fragments)

The Skills module loads markdown skills (with YAML frontmatter) and makes them invocable by the user (`--skill`) or by the model (`skill` tool). See [Source/CodeHarness/Skills/](Source/CodeHarness/Skills/) for source.

**Key interfaces:**
- `SkillParser` — `Parse(content, path, source)` → `SkillDefinition` (YAML frontmatter + markdown body, via `yaml-cpp`)
- `SkillScanner` — `Scan(roots, host)` → recursive directory discovery of `SKILL.md` (folder form) and loose `*.md` (max depth 8, ignores dotfiles/`node_modules`)
- `SkillRegistry` — in-memory store; `LoadRoots`, `Register` (first-registered-wins precedence), `GetSkill`, `ListInvocableSkills`, `RenderSkillPrompt`, `RenderSkillIndex`
- `SkillManager` — `Activate(payload)`; depth-bounded (MAX_DEPTH=3); routes rendered content to the message callback (inline) or system callback (prompt)
- `SkillTool` — `ExecutableTool` named `"skill"` (`{name, args}`); `requiresPermission = false`; activates via the manager with `origin = ModelTool`

**Design:** Skills are discovered from four source roots with `project > user > extra` precedence (first-registered wins): `<cwd>/.agents/skills` (Project, gated by `[skills].allow_project_skills`), `~/.agents/skills` (User), and any `[skills].extra_skill_dirs` entries (Extra). Variable expansion in `RenderSkillPrompt` supports `$ARGUMENTS`, positional `$0..$n`, named `$<arg>` (from `metadata.arguments`), `${KIMI_SKILL_DIR}`, `${KIMI_SESSION_ID}`. Two invocation paths converge on `SkillManager::Activate`: the user path (`Agent::ActivateSkill` / `--skill name[:args]`, `origin = UserSlash`) and the model path (`SkillTool`, `origin = ModelTool`). The Agent advertises the invocable-skill catalog (`RenderSkillIndex`) in the per-turn system prompt so the model learns what skills exist and when to use them — consuming `disableModelInvocation` (filters the catalog) and `whenToUse` (per-skill hint). `SkillType` distinguishes activation target: `Inline` (default) renders as a user message; `Prompt` renders into the turn's system prompt. Out of scope (deferred): `SkillType::Flow` multi-step workflows (parsed/stored, falls back to inline with a warning), nested-skill chaining (`SkillOrigin::NestedSkill` + depth increment), `SkillSource::Builtin`/bundled skills, an interactive TUI `/skill-name` slash surface (only `--skill` today), and MCP ownership. Closes exec-plan #12.

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
