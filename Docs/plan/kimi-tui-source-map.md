# Kimi TUI Source Map

Reference: `MoonshotAI/kimi-code` at `efdf8a1b2d4e906fbb35620083c3e7b490e0e88a`.

This map tracks the 1:1 TUI port from Kimi Code's TypeScript `apps/kimi-code/src/tui/` tree to CodeHarness' C++/FTXUI implementation in `Source/CodeHarness/Tui/`.

## Porting Order

| Order | Kimi area | CodeHarness target | Status | Notes |
| --- | --- | --- | --- | --- |
| 1 | `tui-state.ts`, `types.ts` | `TuiState.h`, `EventRouter.*` | Partial | Current state covers transcript, tools, modal, theme, session, and usage. Missing live-pane and richer app/dialog state. |
| 2 | `commands/*` | `Utils/SlashCommands.*`, `TuiApp::HandleSlashCommand` | Partial | Command catalog and parsing are being expanded first; unsupported commands should surface clear status messages. |
| 3 | `components/chrome/*` | `Components/StatusBar`, `ActivityIndicator`, `TodoPanel`, `QueuePanel`, future banner/welcome | Partial | Current layout has status/activity/todo/queue; missing faithful banner, footer fields, and welcome. |
| 4 | `components/messages/*` | `Components/MessageEntry`, `ToolCallCard`, `ThinkingView`, renderers | Partial | Current transcript supports user/assistant/tool/system; missing dedicated usage/status/plan/skill panels. |
| 5 | `components/dialogs/*` | `Make*Dialog`, future dialog components | Partial | Approval/question/session/settings/help exist; missing model/provider/permission/theme/tasks/undo dialogs. |
| 6 | `controllers/*` | future `Controllers/*` | Not started | Current behavior lives mostly in `TuiApp.cpp`; split after state and commands stabilize. |
| 7 | `reverse-rpc/*` | `OnApproval`, `OnQuestion`, future modal coordinator | Partial | Blocking approval/question callbacks exist; modal queuing/coordinator is missing. |
| 8 | `theme/*` | `Theme/*`, `TuiState::ColorPalette` | Partial | Dark/light palette exists; missing Kimi theme schema and custom loader. |
| 9 | `components/editor/*` | `MakeInputField`, future editor component | Partial | Current input supports submit/history; missing autocomplete, file mentions, paste media, external editor, undo. |
| 10 | `components/panes/*` | `ActivityIndicator`, `QueuePanel`, future panes | Partial | Activity and queue basics exist; missing BTW and task/browser panes. |

## Kimi Slash Command Catalog

The reference command list comes from `apps/kimi-code/src/tui/commands/registry.ts`.

| Command | Aliases | Availability | CodeHarness status |
| --- | --- | --- | --- |
| `/yolo` | `/yes` | always | Partial: toggles Yolo/Manual via current `/mode` logic. |
| `/auto` | | always | Stub needed: Auto mode exists in config but falls back to Manual. |
| `/permission` | | always | Stub/modal needed. |
| `/settings` | `/config` | always | Partial: settings dialog exists, currently model-focused. |
| `/plan` | | always / idle-only for clear | Stub needed. |
| `/swarm` | | idle-only | Stub needed. |
| `/model` | | always | Partial: picker/direct set exist. |
| `/provider` | `/providers` | always | Stub needed. |
| `/btw` | | always | Stub needed. |
| `/help` | `/h`, `/?` | always | Partial: help dialog exists. |
| `/new` | `/clear` | idle-only | Partial: clear context exists; true new session missing. |
| `/sessions` | `/resume` | idle-only | Partial: session picker exists. |
| `/tasks` | `/task` | always | Stub needed. |
| `/mcp` | | always | Stub needed. |
| `/plugins` | | always | Stub needed. |
| `/experiments` | `/experimental` | idle-only | Stub needed. |
| `/reload` | | idle-only | Stub needed. |
| `/reload-tui` | | always | Stub needed. |
| `/compact` | | idle-only | Partial: calls CoreApi compact stub. |
| `/goal` | | mixed | Stub needed. |
| `/init` | | idle-only | Stub needed. |
| `/fork` | | idle-only | Partial: fork exists. |
| `/title` | `/rename` | always | Partial: rename exists, title display missing. |
| `/usage` | | always | Stub/status needed. |
| `/status` | | always | Partial: status line exists. |
| `/feedback` | | always | Stub needed. |
| `/undo` | | idle-only | Stub needed. |
| `/editor` | | always | Stub needed. |
| `/theme` | | always | Stub needed. |
| `/logout` | `/disconnect` | idle-only | Stub needed. |
| `/login` | | idle-only | Stub needed. |
| `/export-md` | `/export` | idle-only | Stub needed. |
| `/export-debug-zip` | | idle-only | Partial backend API exists; TUI command needed. |
| `/exit` | `/quit`, `/q` | always | Partial: exit/quit exist. |
| `/version` | | always | Implemented. |

## Current C++ TUI Anchors

| C++ file | Role |
| --- | --- |
| `TuiApp.cpp` | Main lifecycle, layout, current slash handlers, modal rendering, input submit. |
| `TuiState.h` | Shared UI state for transcript, tools, modal, theme, session, usage. |
| `EventRouter.*` | Core/agent/loop event to state mutation. |
| `Utils/SlashCommands.*` | Slash command parse/catalog. |
| `Components/*` | Current FTXUI components and renderers. |

## Implementation Notes

- Keep Kimi command names and aliases stable even when the backend behavior is not yet available.
- Unsupported commands should render a concise system message instead of being sent to the model.
- Add backend APIs only when a command cannot be represented as a clear UI stub.
- Prefer moving behavior out of `TuiApp.cpp` after command parity is visible and tested.
