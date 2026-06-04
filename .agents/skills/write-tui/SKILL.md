---
name: write-tui
description: Use when writing or modifying the CodeHarness terminal UI, UI event protocol, interactive CLI surface, approvals, slash-command UX, transcript rendering, bottom-pane/composer flows, or TUI tests. This skill adapts the current openai/codex codex-cli TUI architecture to the C++20 CodeHarness codebase.
---

# Write TUI (CodeHarness)

Use this skill for CodeHarness TUI work. The reference design is the current
`openai/codex` `codex-rs/tui` architecture, adapted to this repository's
C++20 modules and existing OpenHarness rewrite plan.

Do not copy Kimi Code paths or TypeScript TUI assumptions into CodeHarness. The
old `apps/kimi-code/src/tui` map is not relevant here.

Before editing, re-scan the local tree because CodeHarness is still evolving:

- `src/codeharness/cli/`
- `src/codeharness/engine/`
- `src/codeharness/commands/`
- `src/codeharness/permissions/`
- `src/codeharness/tools/`
- `src/codeharness/tasks/`
- `src/codeharness/mailbox/`
- `docs/14-ui/cpp20-rewrite.md`
- `docs/OpenHarness/src/openharness/ui/`
- `docs/OpenHarness/frontend/terminal/src/`

Treat `docs/OpenHarness/` as upstream reference material. Do not modify it
unless the task explicitly targets the imported upstream project.

For dialog, selector, input box, status list, or bottom-pane interaction work,
also read [DESIGN.md](./DESIGN.md). That file is the local visual/interaction
spec for this skill.

## Codex CLI Reference Model

The Codex CLI TUI is useful for architecture, not for direct file names or Rust
types. Use these ideas:

- `codex-rs/tui/src/lib.rs`: startup/wiring, embedded or remote app-server
  selection, logging, terminal lifecycle, and final app launch.
- `codex-rs/tui/src/tui.rs`: raw mode, alternate screen, bracketed paste,
  focus events, keyboard enhancement, frame scheduling, terminal cleanup, and
  panic-safe restoration.
- `codex-rs/tui/src/app.rs`: top-level `App` coordinator and event loop. It
  owns routing, session lifecycle, config persistence, background requests, and
  high-level state, but delegates rendering and focused behavior.
- `codex-rs/tui/src/app_event.rs` and `app_event_sender.rs`: typed internal UI
  event bus. Components request app-level actions by emitting events instead of
  reaching into app internals.
- `codex-rs/tui/src/chatwidget.rs`: transcript surface. It consumes protocol
  events, maintains committed history cells plus a mutable active/live cell,
  handles streaming output, and coordinates overlays.
- `codex-rs/tui/src/bottom_pane/`: composer, footer/status, approval overlays,
  popups, selectors, paste bursts, mentions, and state machines.
- `codex-rs/tui/src/history_cell/`, `exec_cell/`, `markdown*`, `render/`,
  `selection_list.rs`, `width.rs`: reusable rendering primitives and snapshot-
  testable transcript cells.

The key architectural lesson is: **protocol events drive app events, app events
drive focused state machines, state machines render small widgets**. Avoid a
single giant TUI class.

## Current CodeHarness Shape

At this point CodeHarness has a non-interactive CLI and an engine event stream,
but no established native TUI module.

Important current anchors:

- CLI entry: `src/codeharness/cli/cli.h` and `src/codeharness/cli/cli.cpp`
- Engine stream: `src/codeharness/engine/engine.h` and `engine.cpp`
- Engine event types: `EngineAssistantTextDelta`, `EngineToolStarted`,
  `EngineToolFinished`, `EngineToolResult`, `EngineError`
- Slash command registry: `src/codeharness/commands/`
- Permission decisions: `src/codeharness/permissions/`
- Tool abstractions: `src/codeharness/tools/`
- Tests: `tests/*_tests.cpp`, doctest, one `codeharness_tests` binary
- Build: CMake + vcpkg, with module targets under `src/codeharness/*`

The CLI currently parses `-p/--prompt`, builds skills, memory, commands, tools,
system prompt, and runs `Engine::run_streaming`. A TUI should factor that
runtime assembly out instead of duplicating it.

## Target Architecture

Prefer this CodeHarness C++ split:

- `src/codeharness/runtime/` or a focused helper in `cli/` first: builds the
  runtime bundle used by print mode, backend mode, and TUI. This should own
  skill loading, memory store, command registry, tool registry, task manager,
  provider construction, permissions, hooks, and prompt-building inputs.
- `src/codeharness/ui/`: UI-neutral protocol and event adaptation. Put
  `FrontendRequest`, `BackendEvent`, JSON serialization, and an `EngineEvent`
  to UI-event bridge here if implementing backend-only compatibility.
- `src/codeharness/tui/`: native terminal UI. Put `TuiApp`, `TuiEvent`,
  event sender/queue, terminal lifecycle, transcript model, bottom pane,
  selection lists, approval prompts, and rendering helpers here.
- `src/codeharness/cli/`: argument parsing and mode selection only. It may
  choose print mode, backend-only mode, or TUI mode, but should not accumulate
  rendering, event-routing, or state-machine logic.

If a full `runtime/` module feels too large for the current change, make a
small extraction near `cli.cpp` first and keep the interface easy to move later.

## Event Flow

Use a typed internal event flow modeled after Codex CLI:

```text
terminal input / background callback / engine callback
  -> TuiEvent
  -> TuiApp event loop
  -> focused state machine update
  -> render transcript + bottom pane
```

Keep these boundaries:

- Engine and tools emit domain events; they do not print UI directly.
- TUI components emit typed app events; they do not mutate top-level app state
  through back pointers.
- Permission UI resolves pending approval requests through an explicit request
  id or handle; it must not block the renderer without a visible pending state.
- Streaming assistant output updates a live transcript cell; completed output is
  committed to history.
- Tool start/result events should have stable ids so transcript rows can merge
  start, output, failure, and completion in one place.

If the existing engine cannot yet ask interactively for permission, extend the
engine/permission interface deliberately rather than special-casing the TUI.
The current `PermissionAction::Ask` path is a known bridge point.

## Feature Routing

Put new work where the behavior belongs:

- CLI flag or mode: `src/codeharness/cli/`, then call runtime/TUI helpers.
- Runtime assembly shared by CLI/TUI/backend: `runtime/` or the smallest
  existing shared layer until a runtime module exists.
- Backend-only JSON Lines compatibility: `src/codeharness/ui/` with
  `OHJSON:` framing if preserving OpenHarness React TUI compatibility.
- Native terminal app loop: `src/codeharness/tui/`.
- Terminal raw mode, alternate screen, resize, focus, paste, cleanup:
  `src/codeharness/tui/terminal.*`.
- Internal UI events and sender helpers: `src/codeharness/tui/tui_event.*`.
- Transcript history cells: `src/codeharness/tui/history_cell.*` or a
  `history_cell/` subfolder once there are several cell types.
- Streaming/active cell logic: a transcript/chat surface module, not `cli.cpp`.
- Composer, slash input, footer, status line, approvals, selectors, popups:
  `src/codeharness/tui/bottom_pane/` or equivalent focused files.
- Markdown, wrapping, width, truncation, ANSI rendering: reusable TUI render
  helpers with focused tests.
- Slash command parsing/execution: keep command definitions in
  `src/codeharness/commands/`; the TUI only renders discovery, input, and
  selection state, then submits through the command registry.

Avoid putting feature logic in terminal event handlers. Key handling should
translate input into an intent; focused state machines handle the intent.

## UI And Interaction Rules

Follow [DESIGN.md](./DESIGN.md) for local dialog/list/input visuals. When
adapting Codex CLI behavior, keep these CodeHarness rules:

- Use a single bottom pane/composer owner for prompt input and transient
  overlays. Do not let independent widgets compete for keyboard focus.
- Model list pickers and approval prompts as explicit states, not ad hoc flags.
- Track busy state from actual lifecycle events: active engine run, pending tool
  approval, pending task/subagent work, or startup.
- Keep interrupt behavior explicit. User-initiated quit should run cleanup when
  possible; an immediate exit path is only a last-resort escape hatch.
- Terminal cleanup is part of correctness: restore raw mode, bracketed paste,
  keyboard modes, cursor state, alternate screen, and trailing newline/prompt
  position.
- Be conservative on Windows terminals. Prefer simple escape handling and test
  with PowerShell/Windows Terminal assumptions.

## CMake And Dependencies

CodeHarness is C++20 with CMake and vcpkg. Add a module the same way nearby
modules are structured:

- `src/codeharness/tui/CMakeLists.txt`
- `codeharness_tui` target, linked into `codeharness_cli` only when the CLI
  needs native interactive mode
- `tests/tui_tests.cpp` or focused additions to existing tests
- update `src/codeharness/CMakeLists.txt` and `tests/CMakeLists.txt`

Do not add a large TUI dependency casually. If choosing a native TUI library,
prefer one mature library for the surface and keep CodeHarness's engine/events
independent of it. The existing docs mention FTXUI as a candidate; verify the
current dependency plan before adding it to `vcpkg.json`.

## Testing

Scale tests with the risk:

- Event mapping: unit-test `EngineEvent` -> TUI/backend event conversion.
- JSON Lines protocol: test valid requests, invalid JSON, unknown type, partial
  line handling, `OHJSON:` output framing, UTF-8, and shutdown.
- Permission flows: test request id correlation and that approve/deny unblocks
  the pending tool path exactly once.
- Composer/key handling: test Enter/newline, slash command input, Esc/cancel,
  paste bursts, and focus switching as pure state-machine tests.
- Rendering: snapshot or golden-text tests for transcript cells, tool results,
  error rows, approval prompts, narrow widths, and CJK/wide characters.
- Terminal lifecycle: cover cleanup on normal exit and errors where practical.
- CLI integration: test mode selection and that `-p` print mode remains quiet
  and non-interactive.

Run the narrowest relevant test first, then the normal project verification:

```powershell
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

If the preset or compiler is unavailable, report that plainly and include the
focused tests or static checks that did run.

## Before Submitting

- Re-read the touched CodeHarness modules and make sure the TUI did not absorb
  engine, tool, command, or permission responsibilities.
- Verify `cli.cpp` is still mostly argument parsing and mode dispatch.
- Check [DESIGN.md](./DESIGN.md) for every dialog, selector, input box, or
  status/toggle list.
- Keep `docs/OpenHarness/` untouched unless explicitly asked.
- Format touched C++ files with the repository formatter.
- Update or add focused doctest coverage for every new protocol/event/state
  branch.
