# Codex CLI TUI replication plan

This document records the current plan for bringing the CodeHarness native TUI
closer to the architecture used by the OpenAI Codex CLI TUI.

Reference snapshot:

- Repository: `D:\code\_refs\openai-codex`
- Commit: `1547785`
- Primary reference tree: `codex-rs/tui/src/`

The goal is not a file-by-file port. Codex CLI is Rust, async, Ratatui, and
app-server based. CodeHarness is C++20, FTXUI, and already has a direct
`RuntimeBundle` path. The useful part to replicate is the boundary design:
protocol events drive app events, app events drive focused state machines, and
state machines render small, testable widgets.

## What Codex CLI TUI does

Codex CLI splits its TUI into these major responsibilities.

| Codex CLI area | Role | CodeHarness equivalent |
| --- | --- | --- |
| `lib.rs` | Startup wiring, config, app-server selection, resume/start flow, logging, and launch | `cli/` plus `runtime/` setup |
| `tui.rs` | Terminal lifecycle: raw mode, paste/focus modes, keyboard enhancement, inline/alternate viewport, frame scheduling, panic-safe restore | New `tui/terminal.*` |
| `app.rs` | Top-level app coordinator and event loop. Owns routing, thread/session lifecycle, async requests, and app shutdown sequencing | New `TuiApp` coordinator layer |
| `app_event.rs`, `app_event_sender.rs` | Internal typed app event bus. Widgets emit actions without mutating app internals directly | New `TuiEvent` and `TuiEventSender` |
| `chatwidget.rs` | Transcript surface. Converts protocol events into committed history cells and mutable streaming cells, and owns the bottom pane | Split current `TuiAppModel` into `ChatSurface` plus `BottomPane` |
| `bottom_pane/` | Composer, slash commands, popups, selectors, approvals, request-user-input, paste bursts, and footer/status | New `tui/bottom_pane/` state machines |
| `history_cell/`, `exec_cell/` | Source-backed transcript rows with stable rendering and tool-row merging | New `tui/history_cell/` |
| `markdown_render/`, `render/`, `width.rs`, `wrapping.rs` | Reusable rendering, wrapping, width, markdown, and test helpers | Expand current `tui_render.*` and `tui_markdown.*` |

Important behavior to copy:

- Engine/protocol events never render UI directly.
- User input, background callbacks, approval responses, and redraw ticks all
  enter the app through typed events.
- The bottom pane owns keyboard focus for the composer and transient overlays.
- Streaming assistant output is mutable while in flight, then finalized into a
  source-backed transcript cell.
- Tool execution rows are correlated by stable ids so start, result, failure,
  and completion update one row.
- Terminal cleanup is correctness work, not polish: raw mode, paste mode, focus
  mode, cursor state, and stdout/stderr behavior must recover on normal exit and
  failure.

## Current CodeHarness state

CodeHarness already has a native TUI target in `src/codeharness/tui/`:

- `TuiAppModel` stores transcript, composer, command palette, model selector,
  permission modal, question modal, busy state, active session, paste bursts,
  and permission mode.
- `run_tui()` starts an FTXUI fullscreen loop, launches prompt execution on a
  worker thread, and uses condition variables for permission and question
  responses.
- `apply_engine_event()` maps `EngineAssistantTextDelta`, `EngineToolStarted`,
  `EngineToolFinished`, `EngineToolResult`, and `EngineError` into transcript
  rows.
- `tui_render.*` and `tui_markdown.*` provide the first reusable render helpers.
- `tests/tui_tests.cpp` already covers modal state, command palette, select
  modal, question modal, paste handling, engine event merging, footer state, and
  markdown parsing.

The main gap is shape, not absence. Current `tui_app.cpp` combines app
coordination, terminal loop, input routing, transcript state, modal state,
runtime calls, and rendering. Codex CLI shows that these concerns should be
split before adding more interactive features.

## Target CodeHarness architecture

Recommended C++ module layout:

```text
src/codeharness/tui/
  tui_app.*                 top-level coordinator and event loop
  tui_event.*               typed app events and sender/queue
  terminal.*                FTXUI/terminal lifecycle wrappers
  chat_surface.*            transcript state and EngineEvent adaptation
  history_cell/
    history_cell.*
    assistant_cell.*
    user_cell.*
    tool_cell.*
    error_cell.*
  bottom_pane/
    bottom_pane.*
    composer.*
    command_palette.*
    model_selector.*
    permission_overlay.*
    question_overlay.*
    paste_burst.*
    footer.*
  render/
    markdown.*
    wrapping.*
    width.*
    theme.*
```

Keep `src/codeharness/runtime/` as the composition root. The TUI should receive
a ready `RuntimeBundle` and should not rebuild tools, commands, permissions,
providers, memory, or sessions.

## Event model

Introduce a typed internal event bus before growing the UI:

```cpp
using TuiEvent = std::variant<
    TuiTerminalInput,
    TuiPaste,
    TuiResize,
    TuiDraw,
    TuiSubmitPrompt,
    TuiEngineEvent,
    TuiRunCompleted,
    TuiPermissionRequested,
    TuiPermissionResolved,
    TuiQuestionRequested,
    TuiQuestionAnswered,
    TuiOpenCommandPalette,
    TuiOpenModelSelector,
    TuiSwitchModelCompleted,
    TuiInterrupt,
    TuiExitRequested>;
```

Rules:

- Terminal handlers translate raw keys into intents only.
- Widgets emit `TuiEvent`; they do not reach into `TuiApp`.
- `TuiApp` is the only owner of runtime submission, cancellation, model
  switching, and shutdown sequencing.
- `ChatSurface` consumes engine events and owns transcript cells.
- `BottomPane` owns the active view stack and focus state.
- Permission and question prompts must be correlated by request id and resolved
  exactly once.

## Replication phases

### Phase 1: split the current monolith

Move behavior without changing visible UI.

- Extract `chat_surface.*` from transcript fields and `apply_engine_event()`.
- Extract `bottom_pane/` state machines from command palette, model selector,
  permission prompt, question prompt, composer, and paste burst logic.
- Extract `terminal.*` for the FTXUI screen setup and cleanup path.
- Add `tui_event.*` and route existing direct calls through events where the
  conversion is straightforward.
- Keep `run_tui()` as the public entry point for now.

Acceptance:

- Existing `tests/tui_tests.cpp` still passes.
- `cli.cpp` still only chooses mode and calls `tui::run_tui()`.
- No runtime assembly moves into `tui/`.

### Phase 2: Codex-style transcript cells

Replace stringly transcript rows with source-backed cells.

- Add `HistoryCell` interface with render-to-lines and optional FTXUI element
  rendering.
- Implement `UserCell`, `AssistantStreamCell`, `AssistantMarkdownCell`,
  `ToolCell`, `ErrorCell`, and `SystemCell`.
- Keep live assistant output mutable while streaming.
- Finalize assistant markdown into a stable source cell after the run completes.
- Preserve stable tool ids and merge tool start/result/finish into the same
  cell.

Acceptance:

- Tool rows do not duplicate when start, result, and finish arrive in different
  orders.
- Failed tool output auto-expands.
- Assistant markdown re-renders correctly at narrow widths.
- Plain-text render tests cover each cell type.

### Phase 3: bottom pane as one focus owner

Copy the Codex CLI focus model.

- Introduce `BottomPane` with one active view at a time.
- Convert command palette, model selector, permission prompt, and question
  prompt into `BottomPaneView`-style state machines.
- Add explicit Esc behavior: clear query first, then cancel.
- Keep submit, interrupt, and quit behavior outside individual views.
- Preserve the local visual rules in `.agents/skills/write-tui/DESIGN.md`.

Acceptance:

- No two overlays can accept keyboard input at the same time.
- Busy state disables normal prompt submission but allows interrupt.
- Permission approve, approve-for-session, and deny resolve one pending request.
- Slash command picker and model picker share the same searchable-list behavior.

### Phase 4: terminal lifecycle hardening

Bring over the reliability lessons from Codex CLI `tui.rs`.

- Add a small RAII terminal session object for raw mode, cursor, paste mode, and
  cleanup.
- Detect non-terminal stdin/stdout and return a normal `Result<int>` error.
- Keep a panic/exception-safe cleanup boundary around the interactive loop.
- Normalize paste events before they reach the composer.
- Keep Windows Terminal and PowerShell behavior conservative.

Acceptance:

- Normal quit restores the terminal and prints one final newline.
- Interrupt during a run cancels the worker and unblocks permission/question
  waiters.
- Cleanup runs when prompt execution returns an error.
- Tests cover pure lifecycle decisions where possible; manual verification
  covers real terminal behavior.

### Phase 5: richer Codex CLI workflows

Only after the split is stable, add user-visible parity features.

- Session picker and resume/fork workflow.
- Better `/model` picker with provider grouping and current-state markers.
- Status/footer sections for branch, cwd, token usage, MCP status, model,
  permission mode, and active session.
- File mention/search popup if the engine path needs it.
- Approval details for command, patch, and permission requests.
- Optional external editor handoff with terminal restore/re-enter.

Acceptance:

- Each new popup has pure state-machine tests and render snapshots.
- Session resume uses `RuntimeBundle::resume_session()` and does not bypass the
  normal engine/session model.
- External editor flow restores and re-enters terminal modes correctly.

## Testing plan

Keep most TUI logic testable without launching an interactive terminal.

- `chat_surface_tests.cpp`: engine event mapping, stream finalization, tool row
  merging, rollback/cancel behavior.
- `bottom_pane_tests.cpp`: composer submit/newline/history, command palette,
  model selector, permission overlay, question overlay, Esc behavior, paste
  bursts.
- `history_cell_tests.cpp`: user/assistant/tool/error/system rendering at wide
  and narrow widths, CJK/wide text, markdown tables, code fences, and truncation.
- `tui_event_tests.cpp`: event routing, request id correlation, shutdown-first
  exit, interrupt unblocks pending prompts.
- Existing `ui_backend_tests.cpp`: keep backend-only JSON Lines independent from
  native TUI.

Normal verification:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

If only TUI code changes, run the focused test binary filter first, then the
full preset.

## Non-goals

- Do not port Ratatui or the Rust app-server implementation.
- Do not put provider, tool, permission, memory, or command behavior inside
  `tui/`.
- Do not copy Codex CLI modules that depend on Codex-specific cloud, auth,
  app-server, voice, pet, or marketplace behavior unless CodeHarness later adds
  matching product requirements.
- Do not expand `docs/OpenHarness/`; it remains upstream reference material.

## Immediate next implementation slice

The first code slice should be deliberately small:

1. Add `tui_event.*` with the internal event types and a simple queue/sender.
2. Extract transcript state into `chat_surface.*`.
3. Move command/model/permission/question state into `bottom_pane/` without
   changing behavior.
4. Keep `run_tui()` rendering through the existing `tui_render.*` helpers.
5. Move the relevant tests out of `tests/tui_tests.cpp` into focused test files
   as each extraction lands.

That slice gives CodeHarness the Codex CLI shape while keeping the current UI
working. After that, richer transcript cells and terminal lifecycle hardening
can land independently.
