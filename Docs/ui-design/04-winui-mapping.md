# 04 — WinUI Mapping & Gap Analysis

The "where am I / what's next" document. Translates CodePilot's React/Tailwind
patterns into WinUI 3 (C++) XAML, maps the icon and animation systems, inventories
the **current** state of `Apps/DesktopWinUI/` file-by-file, and gives a
prioritized build checklist for the 1:1 port.

Pair with [00](00-overview.md)–[03](03-components.md) for the design spec, and
with `docs/coding-conventions.md` for the C++ style.

---

## 1. Mental-model translation: React → WinUI

| React / Tailwind concept | WinUI 3 equivalent |
|--------------------------|--------------------|
| `<div className="flex flex-col">` | `<StackPanel Orientation="Vertical">` or `<Grid>` with `RowDefinitions` |
| `<div className="flex flex-row">` | `<StackPanel Orientation="Horizontal">` or `<Grid>` with `ColumnDefinitions` |
| `<div className="flex flex-1">` | `<Grid>` cell with `RowDefinition Height="*"` / `ColumnDefinition Width="*"` |
| `gap-N` (Tailwind) | `StackPanel Spacing="{N*4}"` (4px base unit) |
| `p-N` / `px-N` / `py-N` | `Padding="left,top,right,bottom"` (N*4 px) |
| `max-w-3xl` (768px) | `MaxWidth="768"` |
| `min-h-0` / `min-w-0` | `MinWidth="0"` / `MinHeight="0"` (allow shrink) |
| `overflow-hidden` | `Border`/`Grid` clips by default; `ScrollViewer` for scroll |
| `rounded-full` (pill) | `CornerRadius="{Binding ActualHeight, Converter=Half}"` or set `CornerRadius` = half the fixed `Height` |
| `rounded-2xl` (24px) | `CornerRadius="24"` |
| `rounded-3xl` (28px) | `CornerRadius="28"` |
| `bg-muted` | `Background="{ThemeResource CodeHarnessSubtleCardBrush}"` |
| `text-muted-foreground` | `Foreground="{ThemeResource CodeHarnessMutedTextBrush}"` |
| `text-xs` (12px) | `FontSize="12"` |
| `font-mono` | `FontFamily="Consolas"` (or bundled Geist Mono / Cascadia Code) |
| `truncate` | `TextBlock TextTrimming="CharacterEllipsis"` |
| `group-hover:opacity-100` | `VisualStateManager` `PointerOver` state on the parent, or `PointerEntered`/`PointerExited` handlers |
| `transition-all` | `Transitions` collection (`ContentThemeTransition`, etc.) or `Storyboard` |
| `motion.div animate={{height}}` | `DoubleAnimation` on `Height` in a `Storyboard` |
| Radix `Collapsible` | `Expander` (WinUI 3) or custom `Grid` with animated `Height` + `Visibility` |
| Radix `DropdownMenu` | `MenuFlyout` |
| Radix `Popover` | `Flyout` (attached to a control) |
| Radix `Dialog` | `ContentDialog` |
| Radix `Tooltip` | `ToolTipService.ToolTip` |
| Radix `Tabs` | `Pivot` or custom tab strip (CodePilot style is custom) |
| `cn()` (clsx + tailwind-merge) | N/A — XAML has no class merging; use `Style` + `BasedOn` + `VisualStateManager` |
| React component | `UserControl` (.xaml + .xaml.cpp/.h) or `ContentControl` with `DataTemplate` |
| React list (`.map()`) | `ItemsControl` / `ItemsRepeater` with `DataTemplate`, bound to `IObservableVector<T>` |
| React `useState` | View-model property with `x:Bind` (one-way/two-way) |
| React context provider | Singleton service / `DependencyProperty` / `x:Static` |

---

## 2. Window chrome

CodePilot Electron (`electron/main.ts:977-1061`):
- Default 1280×860, min 1024×600
- `titleBarStyle: 'hidden'` + `titleBarOverlay` on Windows (height 44, transparent bg)
- 138px reserved for caption buttons in `UnifiedTopBar`

**WinUI 3 approach:**
- `Microsoft.UI.Xaml.Window` with `ExtendsContentIntoTitleBar = true`.
- Set a custom title bar via `SetTitleBar(uiElement)` — the 40px top bar in
  `ShellPage.xaml` row 0 becomes the drag region.
- Reserve caption-button space: the existing `ShellPage.xaml` already reserves
  space on the right via the settings button column; verify ~138px clearance.
- Min size: handle in code (`AppWindow.Closing` or `SizeChanged`) since WinUI
  has no declarative min — or set via `AppWindow.SetMinSize`.
- Backdrop: `MicaController` (SystemBackdrop) for the window — the WinUI
  equivalent of macOS vibrancy. Set on `MainWindow`.

**Current WinUI:** `Apps/DesktopWinUI/MainWindow.xaml.cpp` + `main.cpp` own the
window. Verify `ExtendsContentIntoTitleBar` is set and `SetTitleBar` points at
the top-bar grid.

---

## 3. Icon strategy

CodePilot uses **two SVG icon sets**: HugeIcons (`@hugeicons/core-free-icons`,
primary, ~95 semantic concepts) + Phosphor (`@phosphor-icons/react`, fallback,
used directly in shadcn primitives). Both render as inline SVG.

### WinUI options (ranked)

| Option | Pros | Cons | Recommendation |
|--------|------|------|----------------|
| **Segoe Fluent Icons** (font) | Native, free, ~3000 glyphs, matches Windows 11 aesthetic | Not 1:1 with HugeIcons/Phosphor names; needs mapping | ✅ **Primary** — recommended |
| Segoe MDL2 Assets (font) | Already used in current WinUI | Older, less refined than Fluent Icons | Phase out |
| Bundled SVG icons | Exact 1:1 with CodePilot | Need to bundle + create an icon service | Use for brand/special icons only |
| HugeIcons/Phosphor via WebView2 | Exact 1:1 | Heavy; WebView2 per icon is overkill | ❌ Not worth it |

### Recommended: Segoe Fluent Icons semantic mapping

Migrate from the current `Segoe MDL2 Assets` codepoints to **Segoe Fluent
Icons** (`FontFamily="Segoe Fluent Icons"`). Map the ~30 load-bearing semantic
icon names used in the chat UI:

| CodePilot semantic name | HugeIcons glyph | Segoe Fluent Icons glyph | Unicode |
|------------------------|-----------------|--------------------------|---------|
| `assistant` (Robot01) | Robot01Icon | Bot | `&#xE99A;` |
| `terminal` | TerminalIcon | WindowConsole | `&#xE7C5;`¹ |
| `edit` | PencilEdit02Icon | Edit | `&#xE70F;` |
| `file` | File01Icon | Document | `&#xE8A5;` |
| `search` | Search01Icon | Search | `&#xE721;` |
| `wrench` | ToolKit02Icon | Repair | `&#xE90F;` |
| `cli` (CommandLine) | CommandLineIcon | WindowConsole | `&#xE7C5;` |
| `copy` (Copy01) | Copy01Icon | Copy | `&#xE8C8;` |
| `file_code` | FileCode02Icon | FileCode | `&#xE943;` |
| `folder` | Folder01Icon | Folder | `&#xE8B7;` |
| `folder_open` | FolderOpenIcon | FolderOpen | `&#xE8DA;` |
| `image` | Image01Icon | Photo | `&#xE8B9;` |
| `pin` | Pin01Icon | Pinned | `&#xE840;` |
| `download` | Download04Icon | Download | `&#xE896;` |
| `plus` (PlusSign) | PlusSignIcon | Add | `&#xE710;` |
| `code` | SourceCodeIcon | FileCode | `&#xE943;` |
| `skill` (MagicWand03) | MagicWand03Icon | AutoEnhance | `&#xE8F2;`² |
| `preview` (Eye) | Eye01Icon | View | `&#xE890;` |
| `chat` | Message01Icon | Comment | `&#xE90A;` |
| `stop` | StopCircleIcon | Stop | `&#xE71A;` |
| `play` | Play01Icon | Play | `&#xE768;` |
| `refresh` | Reload01Icon | Refresh | `&#xE72C;` |
| `cancel` | Cancel01Icon | Cancel | `&#xE711;` |
| `model` (Cube) | CubeIcon | Cube | `&#xE950;`³ |
| `runtime` (Chip) | AiChip02Icon | CPU | `&#xE950;` |
| `provider` (Plug02) | Plug02Icon | Plug | `&#xE7F1;` |
| `CaretDown` | Phosphor | ChevronDown | `&#xE70D;` |
| `CaretRight` | Phosphor | ChevronRight | `&#xE76C;` |
| `CaretUp` | Phosphor | ChevronUp | `&#xE70E;` |
| `Check` | Phosphor | CheckMark | `&#xE73E;` |
| `CheckCircle` | Phosphor | CheckMark | `&#xE73E;` (in circle `Border`) |
| `XCircle` | Phosphor | Cancel | `&#xE711;` (in circle `Border`) |
| `SpinnerGap` | Phosphor | (use `ProgressRing`) | — |
| `ArrowUp` | Phosphor | ArrowUp | `&#xE74A;` |
| `ArrowDown` | Phosphor | ArrowDown | `&#xE74B;` |
| `Square` | Phosphor | Stop | `&#xE71A;` |

¹ Verify exact glyph; `WindowConsole` may differ. ² AutoEnhance is a sparkle,
close to MagicWand. ³ Verify Cube glyph exists in Fluent Icons.

**Implementation:** create a `CodeHarnessIcon` XAML markup extension or a
`UserControl` that maps a semantic name → glyph, mirroring CodePilot's
`<CodePilotIcon name="...">`. Size tokens: sm=14, md=16, lg=20, xl=24.

---

## 4. Animation mapping

| CodePilot animation | WinUI approach |
|---------------------|----------------|
| **Shimmer text** (`motion.create("p")` gradient sweep) | `TextBlock` with a `CompositionLinearGradientBrush` whose `ColorStops.Offset` is animated via `ScalarKeyFrameAnimation`; or a `Storyboard` animating a `LinearGradientBrush.StartPoint`/`EndPoint`. Infinite repeat. |
| **Height tween** (`motion.div animate={{height}}`) | `DoubleAnimation` on the container's `Height` (or `RenderTransform.ScaleY`) in a `Storyboard`, with the same easing (`[0.32,0.72,0,1]` ≈ `CubicEase`/`QuinticEase`). |
| **Status dot transitions** (`AnimatePresence mode="wait"`) | `Grid` with overlapping `ProgressRing`/`FontIcon`, swap `Visibility` + `Opacity` via `VisualStateManager` states. |
| **Radix Collapsible open/close** | `Expander` (built-in) OR custom: animate `Height` 0↔auto + `Opacity`. `ContentThemeTransition` for entrance. |
| **Radix Dialog/Popover zoom+fade** | `ContentDialog` / `Flyout` built-in `Popup` transitions; override with `Transitions` collection (`EdgeUIThemeTransition`, `EntranceThemeTransition`). |
| **tw-animate-css `slide-in-from-bottom-2`** | `EntranceThemeTransition` + `TranslateTransform` Y animation. |
| **Press translate** (`active:translate-y-px`) | `RenderTransform` `TranslateTransform` Y=-1 in `Pressed` `VisualState`. |
| **Hover bg swap** | `VisualState` `PointerOver` → `Setter Target="Root.Background"`. (Already done in `Controls.xaml` `PlainButtonTemplate`.) |
| **File-tree / search pulse** | `Storyboard` with `ColorAnimation` on the item background, 2s ease-in-out. |

The existing `Apps/DesktopWinUI/Themes/Controls.xaml` already implements the
hover/press/disabled `VisualStateManager` pattern correctly — extend it for
the new animations.

---

## 5. Markdown rendering strategy

CodePilot uses `streamdown` + Shiki (dual-theme syntax highlighting). WinUI has
no built-in markdown renderer.

### Options (ranked)

| Option | Fidelity | Effort | Recommendation |
|--------|----------|--------|----------------|
| **Markdown-to-XAML converter** (build or port) | High | High | ✅ **Primary** — produce `RichTextBlock` + `InlineUIContainer` for code/tables/images. Pairs with the [02](02-chat-content.md) §10 element styling. |
| `RichTextBlock` + lightweight per-language tokenizer | Medium | Medium | Good for MVP — no full markdown, but handles code blocks well |
| WebView2 + a JS markdown lib (streamdown) | Highest | Medium | Heavy (one WebView per message); use only if pixel-perfect Shiki output is required |
| Plain `TextBlock` (current) | Low | Low | ❌ Insufficient — no formatting |

**Phased approach:**
1. **MVP:** `RichTextBlock` with paragraph/heading/bold/italic/list/code-inline support.
2. **Code blocks:** dedicated `Border` + monospace `TextBlock` (terminal mode = dark bg) + simple tokenizer for bash/json/ts.
3. **Tables/images/links:** `Grid` + `Image` + `Hyperlink` in `RichTextBlock`.
4. **Full markdown:** port or wrap a C++ markdown library (e.g., `md4c`) → XAML tree.

See [02](02-chat-content.md) §10 for exact element styling.

---

## 6. Code blocks strategy

CodePilot `code-block.tsx` uses Shiki with dual light/dark themes + LRU caches.

**WinUI pragmatic approach:**
- **Terminal mode (bash/sh):** dark `Border` (`--terminal-bg` = `#13110F`) +
  monospace `TextBlock` (`--terminal-foreground` = `#DAD7CF`). Header bar with
  language label + copy button. This is ~80% of the visual value with low effort.
- **Code mode (ts/js/py/json):** `Border` + monospace `TextBlock` with a simple
  regex tokenizer coloring keywords/strings/comments/numbers. Not Shiki-grade,
  but readable.
- **Full Shiki:** defer — would need a C++ Shiki port or WebView2.

Header bar: `Border` (bottom border) with language icon (Segoe Fluent Icons
mapping per [02](02-chat-content.md) §9.3) + filename + uppercase badge + Copy/
Markdown/Preview buttons. Collapse >20 lines.

---

## 7. Current WinUI state inventory

File-by-file audit of `Apps/DesktopWinUI/` against the CodePilot spec.

### 7.1 Themes

| File | Status | Notes |
|------|--------|-------|
| `Themes/Colors.xaml` | 🟡 | Light-mode tokens faithful. **Missing:** Dark `ThemeDictionary`, status brushes (success/warning/info + muted/border), terminal brushes, chart brushes. See [01](01-design-tokens.md) §8. |
| `Themes/Typography.xaml` | 🟡 | Verify Geist/Segoe UI Variable + Cascadia/Consolas mapping, font-size scale. |
| `Themes/Controls.xaml` | 🟡 | Button templates (`PlainButtonTemplate`, `GreenTintButtonTemplate`) well-built. **Missing:** full variant set (destructive/outline/secondary/ghost/link), Input/Select pill styles, Badge/Switch/Tabs themes. See [03](03-components.md). |

### 7.2 Views

| File | Status | Notes |
|------|--------|-------|
| `Views/ShellPage.xaml` | ✅ | Shell + top bar + columns + gutters faithful. Workspace tabs stubbed. See [00](00-overview.md). |
| `Views/ChatPage.xaml` | 🟡 | Container + empty state only. **Missing:** transcript fidelity, markdown, tool rows, thinking, code, diff. See [02](02-chat-content.md). |
| `Views/SettingsPage.xaml` | 🟡 | Skeleton. Defer. |

### 7.3 Controls

| File | Status | Notes |
|------|--------|-------|
| `Controls/Sidebar.xaml` | 🟡 | Basic list. **Missing:** project grouping, collapsible sections, session item polish, settings link + update badge. See [03](03-components.md) §8. |
| `Controls/ComposerBox.xaml` | 🟡 | Box + textarea + send button. **Missing:** chip rows (files/dirs/mentions), real model menu, capsule remove buttons, queue affordance, stop state. See [03](03-components.md) §7. |
| `Controls/ToolCallView.xaml` | ❌ | Empty `<Grid/>`. Build the `ToolActionsGroup` per [02](02-chat-content.md) §7. |
| `Controls/PermissionDialog.xaml` | 🟡 | Exists. Verify matches [02](02-chat-content.md) §14 (Alert-style, truncated input, action buttons). |

### 7.4 Services / ViewModels

| File | Status |
|------|--------|
| `Services/DesktopCoreService` | 🟡 bridges to C++ core |
| `Services/DialogService` | 🟡 |
| `Services/UiDispatcher` | 🟡 |
| `ViewModels/ChatViewModel` | 🟡 |
| `ViewModels/MessageViewModel` | 🟡 (text-only) |
| `ViewModels/SessionListViewModel` | 🟡 |

---

## 8. Prioritized build checklist

Ordered by user-visible impact and dependency. Each item references the spec doc.

### Phase A — Design system completion (foundation)
- [ ] **A1.** Complete `Colors.xaml` dark mode (`ThemeDictionary`) per [01](01-design-tokens.md) §1.3. *[01 §8]*
- [ ] **A2.** Add status brushes (success/warning/error/info + muted/border) per [01](01-design-tokens.md) §1.4. *[01 §8]*
- [ ] **A3.** Add terminal brushes (`TerminalBgBrush`, etc.) per [01](01-design-tokens.md) §1.5. *[01 §8]*
- [ ] **A4.** Complete `Controls.xaml` Button variant set (destructive/outline/secondary/ghost/link × sizes) per [03](03-components.md) §1. *[03 §1]*
- [ ] **A5.** Add Input/Select/Badge/Switch pill styles per [03](03-components.md) §3-5. *[03 §3-5]*
- [ ] **A6.** Set up `MicaController` system backdrop on `MainWindow`. *[00 §2]*

### Phase B — Chat content (the biggest gap)
- [ ] **B1.** Build `ToolActionsGroup` UserControl (collapsed header + count badge + vertical-line rows + `ProgressRing`/check/x status) per [02](02-chat-content.md) §7. *[02 §7]*
- [ ] **B2.** Build `ThinkingRow` (icon-swap toggle + mono shimmer summary + expandable body) per [02](02-chat-content.md) §8. *[02 §8]*
- [ ] **B3.** Build markdown-to-XAML renderer MVP (paragraph/heading/bold/italic/list/inline-code) per [02](02-chat-content.md) §10. *[02 §10]*
- [ ] **B4.** Build code-block control (terminal dark mode + header bar + copy + collapse) per [02](02-chat-content.md) §9. *[02 §9]*
- [ ] **B5.** Upgrade user message bubble (24px radius, muted bg, right-aligned, >300px collapse + fade) per [02](02-chat-content.md) §4. *[02 §4]*
- [ ] **B6.** Build streaming shimmer (`CompositionLinearGradientBrush` animation) per [02](02-chat-content.md) §6. *[02 §6]*
- [ ] **B7.** Build `DiffSummary` cards (ArtifactFileCard + "Also modified") per [02](02-chat-content.md) §11. *[02 §11]*
- [ ] **B8.** Build `MediaPreview` (image/video/audio + lightbox) per [02](02-chat-content.md) §12. *[02 §12]*
- [ ] **B9.** Wire auto-follow scroll + scroll-to-bottom button per [02](02-chat-content.md) §2. *[02 §2]*
- [ ] **B10.** Upgrade empty state (brand icon + time-aware greeting at text-3xl) per [02](02-chat-content.md) §15. *[02 §15]*

### Phase C — Composer completion
- [ ] **C1.** Add chip rows (file attachments + directory refs + mentions) per [03](03-components.md) §7.4. *[03 §7.4]*
- [ ] **C2.** Wire real model selector menu (CommandList-style flyout) per [03](03-components.md) §7.5. *[03 §7.5]*
- [ ] **C3.** Add capsule remove buttons + queue affordance + stop state per [03](03-components.md) §7.5. *[03 §7.5]*

### Phase D — Sidebar & top bar polish
- [ ] **D1.** Refactor `Sidebar` for project grouping + collapsible sections per [03](03-components.md) §8. *[03 §8]*
- [ ] **D2.** Polish session list item (CornerRadius 11, h-8, active = primary@12%) per [03](03-components.md) §8.3. *[03 §8.3]*
- [ ] **D3.** Add "..." session menu (Split / Rename / Copy ID / Delete) to top bar per [03](03-components.md) §9. *[03 §9]*

### Phase E — Workspace & right rail
- [ ] **E1.** Refactor workspace column into `WorkspaceSidebar` (TabBar + TabPanel) per [03](03-components.md) §10. *[03 §10]*
- [ ] **E2.** Extract `FileTreePanel` + `AssistantPanel` as additive right-rail cards per [03](03-components.md) §11, [00](00-overview.md) §9. *[00 §9]*
- [ ] **E3.** Build `GitPanel`, `DashboardPanel` content per [03](03-components.md) §11.3-11.4. *[03 §11]*

### Phase F — Icon migration
- [ ] **F1.** Migrate `Segoe MDL2 Assets` → `Segoe Fluent Icons` across all XAML per §3 mapping. *[§3]*

### Phase G — Deferred (post-MVP)
- Settings pages (12 sections) per [03](03-components.md) §14
- Skills/Plugins/MCP UI per [03](03-components.md) §15
- Terminal drawer per [03](03-components.md) §16
- Setup/onboarding wizard per [03](03-components.md) §13
- Split-screen (2 columns) per [00](00-overview.md) §8
- Alternate theme families (11 beyond `default`)

---

## 9. Quick-reference: most-used Tailwind → XAML

The patterns you'll write most often during the port:

```
Tailwind                          →  XAML
─────────────────────────────────────
flex flex-col gap-2               →  <StackPanel Orientation="Vertical" Spacing="8"/>
flex flex-row items-center gap-2  →  <StackPanel Orientation="Horizontal" Spacing="8" VerticalAlignment="Center"/>
flex-1                            →  Grid cell ColumnDefinition Width="*"
max-w-3xl mx-auto                 →  <Grid MaxWidth="768" HorizontalAlignment="Center"/>
rounded-full                      →  CornerRadius="{Height/2}"  (pill)
rounded-2xl                       →  CornerRadius="24"
rounded-md                        →  CornerRadius="14"
bg-muted                          →  Background="{ThemeResource CodeHarnessSubtleCardBrush}"
bg-card                           →  Background="{ThemeResource CodeHarnessCardBrush}"
text-muted-foreground             →  Foreground="{ThemeResource CodeHarnessMutedTextBrush}"
text-xs                           →  FontSize="12"
text-sm                           →  FontSize="14"
font-medium                       →  FontWeight="Medium"
font-mono                         →  FontFamily="Consolas"
truncate                          →  TextTrimming="CharacterEllipsis"
border border-border              →  BorderBrush="{ThemeResource CodeHarnessBorderBrush}" BorderThickness="1"
shadow-sm                         →  (Composition DropShadow or Translation="0,0,4")
group-hover:opacity-100           →  VisualState "PointerOver" on parent → child Opacity=1
```

---

## 10. When in doubt

1. **Grep `D:/code clone/CodePilot/src/app/globals.css`** — every token is there.
2. **Read the cited component** — every claim in [00]–[03] has a `path:line`.
3. **Check the current WinUI file** before building — many shell pieces exist.
4. **Prefer `Style` + `VisualStateManager`** over inline properties (matches
   the existing `Controls.xaml` pattern).
5. **OKLCH → sRGB:** use the hex table in [01](01-design-tokens.md); never
   hand-estimate.
6. **Pill, not square:** when in doubt about radius, CodePilot uses `rounded-full`
   for controls — set `CornerRadius = Height/2`.
