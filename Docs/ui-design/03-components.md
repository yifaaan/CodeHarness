# 03 — Components (Primitives & Panels)

Exact class strings, variants, and structure for every reusable UI primitive
and the major panels (sidebar, top bar, workspace sidebar, right rail). Pair
with [01](01-design-tokens.md) for token values and [04](04-winui-mapping.md)
for XAML translation.

> **Sources:** `src/components/ui/*.tsx` (shadcn primitives),
> `src/components/layout/*.tsx` (panels), `src/components/patterns/*.tsx`
> (app patterns).

---

## 1. Button (`src/components/ui/button.tsx:7-49`)

Base (line 8):
```
inline-flex items-center justify-center gap-1.5 whitespace-nowrap rounded-full
border border-transparent text-sm font-medium transition-all
active:not-aria-[haspopup]:translate-y-px disabled:pointer-events-none
disabled:opacity-50 [&_svg]:pointer-events-none
[&_svg:not([class*='size-'])]:size-4 shrink-0 [&_svg]:shrink-0 outline-none
focus-visible:border-ring focus-visible:ring-ring/30 focus-visible:ring-3
aria-invalid:ring-destructive/20 dark:aria-invalid:ring-destructive/40
aria-invalid:border-destructive
```

**All buttons are pill-shaped (`rounded-full`)** at base level.

### Variants (lines 22-32)

| Variant | Class string |
|---------|-------------|
| `default` (primary) | `bg-primary text-primary-foreground hover:bg-primary/80 [&_svg]:!text-current` |
| `destructive` | `bg-destructive text-white hover:bg-destructive/90 focus-visible:ring-destructive/20 dark:focus-visible:ring-destructive/40 dark:bg-destructive/60 [&_svg]:!text-current` |
| `outline` | `border-border bg-background hover:bg-muted hover:text-foreground dark:bg-transparent dark:hover:bg-input/30` |
| `secondary` | `bg-secondary text-secondary-foreground hover:bg-secondary/80` |
| `ghost` | `hover:bg-accent hover:text-accent-foreground dark:hover:bg-accent/50` |
| `link` | `text-primary underline-offset-4 hover:underline` |

### Sizes (lines 34-42)

| Size | Class string |
|------|-------------|
| default | `h-9 px-4 py-2` (has-svg: `px-3`) |
| `xs` | `h-6 gap-1 px-2 text-xs` |
| `sm` | `h-8 gap-1.5 px-3` |
| `lg` | `h-10 px-6` |
| `icon` | `size-9` |
| `icon-xs` | `size-6` |
| `icon-sm` | `size-8` |
| `icon-lg` | `size-10` |

**Press feedback:** `active:not-aria-[haspopup]:translate-y-px` (1px down on
press, except dropdown triggers).

**WinUI:** a `ControlTheme`/`Style` targeting `Button` with `BasedOn` for
variants via `VisualState`/`VisualStateManager`. Pill = `CornerRadius={Height/2}`.
Press translate = `RenderTransform` `TranslateY` on `Pressed` state. Current
WinUI `Controls.xaml` has `TopBarIconButtonStyle`, `TopBarPillButtonStyle`,
`SendButtonStyle`, etc. — extend to a full variant set.

---

## 2. Card (`src/components/ui/card.tsx`)

| Part | Class string |
|------|-------------|
| **Card** (line 11) | `bg-card text-card-foreground flex flex-col gap-6 overflow-hidden rounded-3xl py-6 shadow-md ring-1 ring-foreground/5 dark:ring-foreground/10` |
| CardHeader (line 23) | `@container/card-header grid auto-rows-min grid-rows-[auto_auto] items-start gap-2 px-6 has-data-[slot=card-action]:grid-cols-[1fr_auto] [.border-b]:pb-6` |
| CardTitle (line 35) | `leading-none font-semibold` |
| CardDescription (line 45) | `text-muted-foreground text-sm` |
| CardContent (line 68) | `px-6` |
| CardFooter (line 78) | `flex items-center px-6` |

**28px radius**, `shadow-md`, subtle `ring-1` for definition.

**WinUI:** `Border` with `CornerRadius=28`, `Background={CardBrush}`,
`BorderBrush` at low alpha (`ring-foreground/5`), `Shadow`. The floating-card
`Border`s in `ShellPage.xaml` already use this pattern (✅).

---

## 3. Input / Textarea / Select

### Input (`src/components/ui/input.tsx:12-14`)
```
file:text-foreground placeholder:text-muted-foreground
selection:bg-primary selection:text-primary-foreground bg-input/50 h-9 w-full
min-w-0 rounded-3xl border border-transparent px-3 py-1 text-base ... md:text-sm
focus-visible:border-ring focus-visible:ring-ring/30 focus-visible:ring-3
```
**`rounded-3xl` (28px) + `bg-input/50` (50% input color) + transparent border.**
Very pill-shaped.

### Textarea (`textarea.tsx:10`)
`rounded-md border bg-transparent px-3 py-2 min-h-16 shadow-xs focus-visible:ring-[3px]`
(different shape than Input — **14px radius**, not pill).

### Select (`select.tsx`)
- **Trigger** (line 40): `bg-input/50 rounded-3xl border border-transparent px-3 py-2 text-sm` + `data-[size=default]:h-9 data-[size=sm]:h-8`, with `<CaretDown className="size-4 opacity-50" />`.
- **Content** (line 65): `bg-popover ... rounded-md border shadow-md` + zoom animations.
- **Item**: `focus:bg-accent ... rounded-sm py-1.5 pr-8 pl-2 text-sm`.

**WinUI:** `TextBox` style with `CornerRadius=28`, `Background={InputBrush}` at
50% (use a separate `InputSoftBrush`). `ComboBox` for select — WinUI's default
`ComboBox` is squarish; restyle to pill via `ControlTheme`.

---

## 4. Badge (`src/components/ui/badge.tsx:8`)

```
rounded-full border border-transparent px-2 py-0.5 text-xs font-medium
```

| Variant | Class |
|---------|-------|
| default | `bg-primary text-primary-foreground` |
| secondary | `bg-secondary text-secondary-foreground` |
| destructive | `bg-destructive text-white ... dark:bg-destructive/60` |
| outline | `border-border text-foreground` |

---

## 5. Switch / Tabs / Tooltip / Popover / Dialog / Alert / Separator / Progress / Label

| Component | Key class string |
|-----------|-----------------|
| **Switch** (`switch.tsx:20`) | `rounded-full border-2 ... data-[state=checked]:bg-primary data-[state=unchecked]:bg-input/90`; default `h-5 w-11`, sm `h-4 w-7`; thumb `rounded-full bg-background ... data-[state=checked]:translate-x-[calc(100%-8px)]`, default `h-4 w-6`, sm `h-3 w-4` |
| **Tabs** (`tabs.tsx:33`) | TabsList base `rounded-full p-1 ... text-muted-foreground`, default variant `bg-muted`, line variant `gap-1 bg-transparent`; Trigger `rounded-full border border-transparent! px-3 py-1 text-sm font-medium`, active `data-[state=active]:bg-background ... dark:data-[state=active]:bg-input/30 data-[state=active]:text-foreground` |
| **Tooltip** (`tooltip.tsx:45`) | `bg-foreground text-background ... rounded-md px-3 py-1.5 text-xs text-balance` with arrow (**inverted colors**: dark bg, light text) |
| **Popover** (`popover.tsx:47`) | `rounded-3xl bg-[var(--platform-surface-popover)] backdrop-blur-md p-4 text-sm ... shadow-lg ring-1 ring-foreground/5 dark:ring-foreground/10` + zoom/slide animations |
| **Dialog** (`dialog.tsx`) | Overlay `bg-black/50` + fade/zoom; Content `bg-background ... max-w-[calc(100%-2rem)] max-h-[calc(100vh-4rem)] ... rounded-lg border p-6 shadow-lg duration-200 ... sm:max-w-lg`; Title `text-lg leading-none font-semibold`; Description `text-muted-foreground text-sm` |
| **Alert** (`alert.tsx:7`) | `rounded-lg border px-4 py-3 text-sm grid ...`; default `bg-card text-card-foreground`; destructive `text-destructive bg-card` |
| **Separator** (`separator.tsx:20`) | `bg-border ... h-px w-full` (horizontal) |
| **Progress** (`progress.tsx:18`) | `relative flex h-3 w-full ... rounded-full bg-muted`, indicator `size-full flex-1 bg-primary transition-all` |
| **Label** (`label.tsx:16`) | `flex items-center gap-2 text-sm leading-none font-medium select-none` |

**WinUI:** `ToggleSwitch` restyle, `Pivot`/`TabView` restyle to pill tabs,
`ToolTip` theme, `Flyout`/`ContentDialog` for popover/dialog. WinUI
`ContentDialog` is already close to the Dialog spec.

---

## 6. InputGroup (composer box primitive, `ui/input-group.tsx`)

```
border-input dark:bg-input/30 relative flex w-full items-center rounded-2xl
border shadow-sm transition-[color,box-shadow]
```
**24px-radius bordered rounded box with soft shadow.** Focus = neutral border
only: `has-[[data-slot=input-group-control]:focus-visible]:border-border`.

InputGroup buttons override: `text-sm shadow-none flex gap-2`, with sizes
(`xs` `h-6 rounded-[calc(var(--radius)-5px)]`, `sm` `h-8 rounded-md`,
`icon-xs` `size-6`, `icon-sm` `size-8`).

---

## 7. Composer (deep-dive)

Composed of `MessageInput` (logic) wrapping `PromptInput` primitives.

### 7.1 Outer shell (`MessageInput.tsx:1016`)
```
bg-[var(--platform-surface-bar)] backdrop-blur-lg px-4 pt-2 pb-1
```
Inner: `mx-auto w-full max-w-3xl`, then `relative` container holding
`SlashCommandPopover`, `CliToolsPopover`, `QuickActions`, and the `PromptInput`.

### 7.2 The box
See §6 InputGroup. Chat adds
`[&_[data-slot=input-group]]:shadow-[var(--shadow-diffuse)]`.

### 7.3 Textarea
`PromptInputTextarea` → `InputGroupTextarea`:
`flex-1 resize-none rounded-none border-0 bg-transparent py-3 shadow-none focus-visible:ring-0`;
in chat `min-h-12 px-4 py-3`. Enter submits, Shift+Enter newline, Backspace on
empty removes last attachment, paste of files adds them as attachments.

### 7.4 Chip rows (above textarea, via flex `order-first`)
Each `flex w-full flex-wrap items-center gap-1.5 px-3 pt-2/2.5 pb-0 order-first`,
renders zero DOM when empty. All capsule types share the style:
```
inline-flex items-center gap-1.5 rounded-full border border-border/40 bg-muted
pl-2/2.5 pr-1/1.5 py-0.5/1 text-xs font-medium text-foreground
```

| Capsule type | Contents |
|--------------|----------|
| `ComposerBadgeRow` / `CliBadge` | icon + label + remove X |
| `FileAttachmentsCapsules` | `h-5 w-5 rounded object-cover` thumbnail or `file` icon (12px muted), name truncated `max-w-[120px] text-[11px]`, optional size estimate `text-[10px] font-normal text-muted-foreground`, circular X remove (`ml-0.5 h-auto w-auto rounded-full p-0.5 hover:bg-accent`) |
| `DirectoryRefsCapsules` | `folder` icon + `font-mono` path |
| mention capsules | @ + name |

### 7.5 Footer (`PromptInputFooter`)
`justify-between gap-1`. Split into **Tools** (left) and **Submit** (right).

**`PromptInputTools`** = `flex min-w-0 items-center gap-1`:
1. **`PromptInputActionMenu`** — `DropdownMenu`, trigger = ghost `PromptInputButton` showing `<CodePilotIcon name="plus">` (PlusSignIcon, md). Menu: "Add photos or files" (image icon), "Insert command" (code icon), "Call CLI" (cli icon).
2. **`ModelSelectorDropdown`** — `PromptInputButton` (ghost) showing model label in `text-xs font-mono` (+ "· Default" suffix when global default), with `CaretDown` (10px) flipping 180° when open. Disabled → "Preparing runtime…" loading text. Opens custom `CommandList w-80 mb-1.5 animate-in fade-in-0 zoom-in-95 slide-in-from-bottom-2 duration-150` with recent + provider groups; incompatible rows disabled with hover tooltips.
3. **`EffortSelectorDropdown`** — only when model supports effort; "Auto" default.

**Submit button** — `FileAwareSubmitButton` wraps `PromptInputSubmit` with
`className="rounded-full"`:
- `PromptInputSubmit` (`prompt-input.tsx:1144`): `InputGroupButton` (default variant, `icon-sm` = `size-8 p-0`).
- Icon by status: ready = `ArrowUp` (16px); streaming/submitted = `Square`/`Stop` (16px); error = `X`. While generating → stop button (`type="button"`, calls `onStop`).
- Disabled unless text/badge/files. During streaming with non-empty plain text → "queue" affordance (ArrowUp, sends as queued message).

### 7.6 Quick suggestion chips (`QuickActions.tsx`)
Assistant-only, pre-first-message: `flex flex-wrap gap-2 px-1 pb-2`. Each chip =
outline `Button size="xs"`,
`rounded-full border-border/50 bg-background text-muted-foreground hover:border-primary/30 hover:bg-primary/5 hover:text-foreground`,
with `skill` icon (MagicWand03Icon, 12px, `text-primary/60`).

**WinUI:** `ComposerBox.xaml` exists (🟡). Gaps: no chip rows (attachments/dirs/
mentions), no real model menu (button is a stub), no capsule remove buttons,
no queue affordance. The current send button uses MDL2 `&#xE72A;` (send) —
should switch to ArrowUp (`&#xE72A;` is actually arrow-up in Segoe MDL2, so OK).
Add a stop state (`&#xE71A;` Square or `&#xE711;`).

---

## 8. Sidebar (`ChatListPanel`)

Source: `src/components/layout/ChatListPanel.tsx:438-845`. Vertical flex column:

### 8.1 Top action area (`p-2`)
- **New Conversation** button
- **Search (⌘K)** button
- Feature nav links (`/plugins`, `/gallery`)

### 8.2 Scrollable section list (`ScrollArea flex-1`)
Two collapsible sections:
- **项目 (Projects)** — grouped by working directory, each group collapsible,
  with "New Project" entry. Sessions truncate at 10 (`SESSION_TRUNCATE_LIMIT`).
- **助理 (Assistant)** — flat list.

Uses `motion` for collapse animations. Session groups auto-collapse except the
most-recently-active project (`ChatListPanel.tsx:401-421`).

### 8.3 Session list item (`SessionListItem.tsx:72-80`)
```
flex items-center gap-2 rounded-xl px-3 h-8 transition-all duration-150 min-w-0
```
- **isWorkspace + active:** `bg-primary/[0.12] text-sidebar-accent-foreground`
- **isWorkspace + inactive:** `text-sidebar-foreground hover:bg-primary/[0.10]`
- (non-workspace uses sidebar-accent for active)

Hover reveals action buttons (delete `X`, rename, add-to-split `Columns`, menu
`DotsThree`). Active session shows a streaming/approval indicator.

### 8.4 Project group header (`ProjectGroupHeader.tsx`)
Collapsible header with chevron + folder icon + project name (basename of working
dir) + session count.

### 8.5 Bottom
Settings link (`/settings`) with update badge.

**WinUI:** `Controls/Sidebar.xaml` exists (🟡). Needs: project grouping,
collapsible sections, session item style refinement (CornerRadius 11, h-8,
active = primary@12% alpha → `#1F252525`), hover-reveal action buttons.

---

## 9. Top bar (`UnifiedTopBar`) — chat-detail mode

Source: `src/components/layout/UnifiedTopBar.tsx:221-444`. See [00](00-overview.md)
§6. Key elements:

| Zone | Elements |
|------|----------|
| Left | sidebar toggle (≡), session title → project name (muted, opens folder), "..." dropdown (Split / Rename / Copy ID / Delete) |
| Center | `flex-1` spacer |
| Right | Git branch label (click → Git tab), File Tree toggle, Workspace Sidebar toggle; Windows reserves 138px for caption buttons |

**WinUI:** `ShellPage.xaml` row 0 (✅). The git branch pill, files toggle,
workspace toggle, settings button are all present. Gaps: "..." session menu,
split-screen action, real title binding.

---

## 10. WorkspaceSidebar

Source: `src/components/layout/WorkspaceSidebar/`. Width 360 default, resizable,
`kind="workspace"`. Renders `<TabBar/>` + `<TabPanel/>`.

### 10.1 TabBar (`TabBar.tsx`)
`role="tablist"` row. Layout: `[git] [widget] · [dynamic tabs...] · [collapse]`.
- Fixed tabs (git, widget): `shrink-0`
- Dynamic tabs: `flex-1 min-w-[40px] max-w-[160px]`
- WAI-ARIA tabs: ArrowLeft/Right/Home/End keyboard nav
- Close button folds into leading icon (hover swaps file icon → X)
- Collapse button pinned far right

### 10.2 TabPanel (`TabPanel.tsx:41-58`)

| Tab id | Content |
|--------|---------|
| `git` (fixed) | `GitTabContent` (`panels/GitPanel.tsx`) — refresh button + Git panel |
| `widget` (fixed) | `WidgetTabContent` / `DashboardPanel` (`panels/DashboardPanel.tsx`) — widget grid + assistant summary |
| `files-pinned` | `FileTreePanel variant="sidebar"` |
| `markdown` / `file` / `artifact` | `PreviewPanel` (`panels/PreviewPanel.tsx`, 2000+ lines) — file/code/markdown/HTML/diff/JSON/datatable preview with Source/Rendered/Edit modes, autosave |

**WinUI:** `ShellPage.xaml` workspace column (🟡). Git/Progress/Files tab
buttons exist with stub content. Needs: widget/dashboard tab, dynamic preview
tabs, proper tab switching via `Visibility` or `Frame` navigation.

---

## 11. Right-rail panels (`PanelZone`)

Source: `src/components/layout/PanelZone.tsx:77-106`. **Additive** — both can be
open. See [00](00-overview.md) §9.

### 11.1 AssistantPanel (`panels/AssistantPanel.tsx`)
Width 260–460, default 320. Assistant summary card: name, avatar, status.

### 11.2 FileTreePanel (`panels/FileTreePanel.tsx`)
Width 220–500, default 280. VS-Code-style file browser:
- Action icons row (new file / new folder / refresh)
- Optional new-item input
- `FileTree` component (recursive, expand/collapse, file icons)

### 11.3 DashboardPanel (`panels/DashboardPanel.tsx`)
Widget grid + assistant summary. Used in the `widget` tab.

### 11.4 GitPanel (`panels/GitPanel.tsx`)
Refresh button + Git panel (branch, changes, file list).

### 11.5 PreviewPanel (`panels/PreviewPanel.tsx`, 2000+ lines)
Large component: file/code/markdown/HTML/diff/JSON/datatable preview with
Source/Rendered/Edit view modes, autosave.

**WinUI:** the workspace column in `ShellPage.xaml` currently holds Git/Files
inline. To match CodePilot, refactor so the workspace column = WorkspaceSidebar
(tabs), and AssistantPanel + FileTreePanel are **separate additive cards**
(columns) to the right. This is a structural change — see [04](04-winui-mapping.md).

---

## 12. App patterns (`src/components/patterns/`)

| Pattern | Source | Class string |
|---------|--------|-------------|
| **SettingsCard** | `SettingsCard.tsx:13` | `rounded-lg bg-card border border-border/50 p-5 space-y-4` (title `text-sm font-medium`, description `text-xs text-muted-foreground`) |
| **FieldRow** | `FieldRow.tsx:14` | `flex items-center justify-between gap-4`, separator variant `border-t border-border/50 pt-4` |
| **StatusBanner** | `StatusBanner.tsx:4-9` | `flex items-center gap-2 rounded-md px-3 py-2 text-xs`, variant bg `bg-status-success-muted text-status-success-foreground` etc. |
| **SectionPage** | `SectionPage.tsx` | `mx-auto w-full space-y-6 max-w-xl|2xl|3xl` |
| **IconAction** | `IconAction.tsx` | Button variant=ghost size=icon, sm=`h-7 w-7`, md=`h-8 w-8`, wrapped in Tooltip |
| **CommandList** | `CommandList.tsx:25-26` | `rounded-2xl border bg-popover shadow-[var(--shadow-diffuse)]` — composer-matched popover surface |
| **EmptyState** | `EmptyState.tsx` | centered icon + title + description + optional action |

---

## 13. Setup / onboarding (`src/components/setup/`)

| Component | Purpose |
|-----------|---------|
| `WelcomeCard` | First-run welcome |
| `ProviderCard` | Provider connection card |
| `ClaudeCodeCard` | Claude Code CLI setup |
| `ProjectDirCard` | Project directory picker |
| `SetupCard` | Generic setup step card |
| `SetupCenter` | Orchestrates the setup flow |

**WinUI:** not yet implemented. Defer until core chat is complete.

---

## 14. Settings sections (`src/components/settings/`)

12 sections per `nav-config.ts:53-68`: Overview, General, Appearance, Providers,
Models, Runtime, Health, Usage, Assistant, Tasks, Bridge, About.

Key sub-components: `ProviderManager`, `ProviderForm`, `ProviderCard`,
`ProviderDoctorDialog`, `OpenRouterSearchDialog`, `ModelsSection`,
`RuntimePanel`, `RuntimeCapabilityList`, `HealthSection`, `UsageStatsSection`,
`OverviewSection`, `OverviewHeatmap`, `GeneralSection`, `AppearanceSection`,
`AboutSection`, `TasksSection`, `AssistantWorkspaceSection`,
`WorkspaceStatusCards`, `WorkspaceTabPanels`, `WorkspaceConfirmDialogs`,
`CodexAccountModelsBlock`, `CodexQuotaWidget`, `PresetConnectDialog`,
`OpenRouterCleanupDialog`.

**WinUI:** `Views/SettingsPage.xaml` exists (🟡 skeleton). Defer detailed spec
until core chat + workspace panels are complete.

---

## 15. Skills / Plugins UI (`src/components/skills/`, `src/components/plugins/`)

### Skills
`SkillsManager`, `SkillListItem`, `SkillDetailDialog`, `CreateSkillDialog`,
`MarketplaceBrowser`, `MarketplaceSkillDetail`, `InstallProgressDialog`.

### Plugins / MCP
`PluginCard`, `PluginDetail`, `PluginList`, `McpManager`, `McpServerDetailDialog`,
`McpServerEditor`, `McpServerEditorForm`, `McpServerList`, `BuiltInMcpSection`,
`ConfigEditor`, `McpJsonConfigDialog`.

**WinUI:** not yet implemented. These are feature pages (`/skills`, `/plugins`,
`/mcp`), reachable from the sidebar nav. Defer.

---

## 16. Terminal drawer (`src/components/terminal/`)

`TerminalDrawer` + `TerminalInstance` — a slide-up terminal panel (xterm.js
based). Used for inline command execution preview.

**WinUI:** defer (WinUI has no xterm equivalent; would need a `TermControl`
from the Terminal repo or a WebView2).

---

## WinUI component mapping summary

| CodePilot primitive | WinUI equivalent | Current state |
|---------------------|------------------|---------------|
| Button (variants) | `Button` + `Style`s/`ControlTheme` | 🟡 partial (TopBar/Send styles exist) |
| Card | `Border` + shadow | ✅ floating cards done |
| Input / Textarea | `TextBox` restyle | ❌ not pill-styled |
| Select | `ComboBox` restyle | ❌ |
| Badge | `Border` + `TextBlock` | ❌ |
| Switch | `ToggleSwitch` restyle | ❌ |
| Tabs | `Pivot`/`TabView` or custom | 🟡 workspace tabs stubbed |
| Tooltip | `ToolTip` theme | (default) |
| Popover | `Flyout` / `ContentDialog` | ❌ |
| Dialog | `ContentDialog` | 🟡 `PermissionDialog` |
| Alert | `Border` + `TextBlock` | ❌ |
| Composer (InputGroup) | `UserControl` `ComposerBox` | 🟡 partial |
| Sidebar | `UserControl` `Sidebar` | 🟡 partial |
| WorkspaceSidebar | workspace col in `ShellPage` | 🟡 stubbed |
| PanelZone (additive) | separate columns | ❌ needs refactor |

See [04](04-winui-mapping.md) for the full TSX→XAML translation tables and the
prioritized build checklist.
