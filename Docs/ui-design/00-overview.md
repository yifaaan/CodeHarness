# 00 — Architecture & Window Layout

The high-level shape of the CodePilot window: tech stack, window chrome, the
hierarchical panel arrangement, the "floating card" primitive system, and the
navigation model. This is the map; tokens live in [01](01-design-tokens.md),
chat internals in [02](02-chat-content.md), individual components in
[03](03-components.md).

---

## 1. Tech stack

CodePilot is a **Next.js 16 (App Router) + Electron 40** desktop app.

| Layer | Technology | Source |
|-------|-----------|--------|
| Shell | Electron 40 (BrowserWindow, frameless titlebar) | `electron/main.ts` |
| UI framework | React 19 | `package.json` |
| Routing | Next.js App Router (URL-based) | `src/app/` |
| Styling | Tailwind CSS v4 (CSS-based config, **no `tailwind.config.js`**) | `src/app/globals.css` |
| Component primitives | shadcn/ui (style `"radix-luma"`, baseColor `"stone"`) + Radix UI | `components.json`, `src/components/ui/` |
| Animation | `motion/react` (Framer Motion) + `tw-animate-css` | imported throughout |
| Markdown | `streamdown` + Shiki (dual light/dark themes) | `ai-elements/message.tsx`, `code-block.tsx` |
| Icons | `@hugeicons/core-free-icons` (primary) + `@phosphor-icons/react` (fallback) | `ui/semantic-icon.tsx`, `ui/icon.tsx` |
| Fonts | Geist + Geist Mono (Google Fonts via `next/font`) | `app/layout.tsx:13-21` |
| Theming | `next-themes` + custom `ThemeFamilyProvider` (12 families) | `layout/ThemeProvider.tsx`, `ThemeFamilyProvider.tsx` |

**Dark mode** is class-based: `.dark` on `<html>`, toggled by `next-themes`.
A separate `data-theme-family` attribute (default `"default"`) selects one of
12 palettes; only the `default` family is spec'd here.

---

## 2. Window chrome (Electron `BrowserWindow`)

Source: `electron/main.ts:977-1061`.

| Property | Value |
|----------|-------|
| Default size | **1280 × 860** |
| Minimum size | **1024 × 600** |
| macOS titlebar | `titleBarStyle: 'hiddenInset'`, traffic lights at `{x:20, y:21}` (aligned with the 40px top bar) |
| Windows titlebar | `titleBarStyle: 'hidden'` + `titleBarOverlay` (transparent bg, gray symbols, height 44) |
| macOS body | transparent → NSWindow vibrancy shows through; sidebar surfaces are translucent |
| Windows/Linux body | opaque, no vibrancy |

### Platform profile on Windows (important for the port)

CodePilot defines a `--platform-*` token layer in `globals.css:230-391`, gated on
`data-platform` / `data-shell` attributes set by an anti-FOUC script
(`layout.tsx:71`). On **Windows (`win32`)** the *only* override is
`--platform-titlebar-safe-area: 44px` (`globals.css:325-327`). Windows otherwise
uses the default product tokens: **opaque surfaces, no backdrop blur, no rounded
floating cards**. The macOS "Liquid Glass" treatment (translucent sidebar,
`backdrop-blur-xl`, 14px card radius, shell inset padding) is **macOS-only**.

**WinUI implication:** the port targets the Windows profile — opaque cards,
`MicaController` for the window backdrop (the WinUI equivalent of vibrancy),
and a custom 40px title bar with 138px reserved for caption buttons (matching
`titleBarOverlay`). See [04](04-winui-mapping.md) §"Window chrome".

---

## 3. The app shell — entry chain

There is no `src/main.tsx` / `index.html` (it's App Router). The chain is:

```
src/app/layout.tsx          ← root <html>/<body>, providers, anti-FOUC attrs
  └─ <ThemeProvider>         (next-themes: light/dark)
     └─ <ThemeFamilyProvider> (data-theme-family + injected <style>)
        └─ <I18nProvider>
           └─ <IconProvider>
              └─ <AppShell>    ★ THE WINDOW SHELL — src/components/layout/AppShell.tsx
                 └─ {children}  ← active route's page.tsx
```

`AppShell.tsx` is the single most important layout file. It renders the full
window chrome and provides all cross-cutting contexts (`PanelContext`,
`WorkspaceSidebarProvider`, `SplitContext`, `BatchImageGenContext`,
`UpdateContext`).

---

## 4. Hierarchical window layout

Source: `AppShell.tsx:705-762`. The top-level DOM:

```
<div flex flex-col h-screen> [data-app-shell]
├─ <UnifiedTopBar/>                    h-10 (40px), draggable
├─ <UpdateBanner/>                     conditional
└─ <div flex flex-1 min-h-0> [data-app-content-row]   ← horizontal
   ├─ LEFT  CardFrame kind="sidebar"      (resizable 180–300, default 240)
   │        └─ ChatListPanel  OR  SettingsSidebar   (route-switched)
   ├─ ResizeGutter (8px)
   ├─ CENTER CardFrame kind="main"        flex-1 (fills remaining width)
   │  └─ <main>
   │     ├─ /chat hero (new conversation)            ┐
   │     ├─ /chat/[id] view (active conversation)    │ route-switched
   │     ├─ SplitChatContainer (2 columns)           │
   │     ├─ /settings/*                              │
   │     └─ /plugins, /gallery, /skills, ...         ┘
   ├─ ResizeGutter (8px)                              [chat-detail routes only]
   ├─ RIGHT-INNER CardFrame kind="workspace" (default 360)  [chat-detail only]
   │  └─ WorkspaceSidebar (TabBar + TabPanel)
   └─ RIGHT-OUTER PanelZone                            [chat-detail only]
      ├─ CardFrame kind="assistant" (260–460, default 320)  [if assistantPanelOpen]
      └─ CardFrame kind="fileTree"  (220–500, default 280)  [if fileTreeOpen]
```

### ASCII diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│  UnifiedTopBar  (40px)   [≡] title  · project ▾          [git][📁][▣][⚙] │
├─────────┬──────────────────────────────────────────┬──────────┬──────────┤
│         │                                          │          │          │
│ Sidebar │              Main content                │ Workspace│ PanelZone│
│ 240px   │              (chat / settings / ...)     │ Sidebar  │ (assist- │
│ (180–   │              flex-1                      │ 360px    │  ant +   │
│  300)   │                                          │ (tabs:   │  file-   │
│         │                                          │  git wid │  tree)   │
│ Chat-   │                                          │  get     │          │
│ List    │                                          │  files)  │          │
│ or      │                                          │          │          │
│ Settings│                                          │          │          │
│ Sidebar │                                          │          │          │
│         │                                          │          │          │
├─────────┴──────────────────────────────────────────┴──────────┴──────────┤
│ ← 8px gutters between every adjacent card →                              │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key layout rules

1. **Outer container** is `flex flex-col h-screen overflow-hidden`. The content
   row is `flex flex-1 min-h-0 overflow-hidden` (horizontal flex). Source:
   `AppShell.tsx:705`, `:714`.
2. **Sidebar is always visible** when `chatListOpen` (auto-set true at the `lg`
   breakpoint = 1024px, `AppShell.tsx:113`). Resizable: min 180, max 300,
   default 240 (`CHATLIST_MIN`/`MAX`, `AppShell.tsx:93-94`; resize handlers
   `:325-333`).
3. **Main column is `flex-1 min-w-0`** (fills remaining width, can shrink).
   All side panels are `shrink-0` with fixed pixel widths.
4. **Right-rail panels are additive, not mutex** (`AppShell.tsx:116-147`, the
   "v13" rule): the workspace sidebar, file tree, AND assistant panel can all
   be open simultaneously. The chat area shrinks to fit. **Do not** treat them
   as a tabbed/mutually-exclusive group.
5. **Right rails mount only on chat-detail routes** (`isChatDetailRoute`,
   `AppShell.tsx:395`). On `/chat` (new), `/settings/*`, `/plugins`, etc. only
   the sidebar + main render.
6. **macOS-only inset:** on darwin/Electron, `[data-app-shell]` gets
   `padding: 8px 16px 16px 16px; gap: 4px` (`globals.css:430-435`) so all four
   cards float with breathing room. Windows/web/Linux: no padding, no gap.
7. **Six heavy components are lazy-loaded** via `next/dynamic` with `ssr:false`
   (`AppShell.tsx:39-62`): `SetupCenter`, `SplitChatContainer`,
   `WorkspaceSidebar`, `PanelZone`, `UpdateDialog`,
   `FeatureAnnouncementDialog`.

---

## 5. The "floating card" primitive system

Source: `src/components/layout/card-primitives.tsx`. Three primitives implement
the macOS-style floating-card look:

### `CardFrame` (`card-primitives.tsx:78-96`)
The outer frame. `h-full shrink-0`. For `kind="main"` it adds `flex-1 min-w-0`
to fill remaining space. Accepts a fixed `width` prop for side panels. Emits
`data-platform-card-frame`.

| `kind` | Default width | Min | Max | Surface |
|--------|--------------|-----|-----|---------|
| `sidebar` | 240 | 180 | 300 | translucent (`--platform-surface-sidebar` + `backdrop-blur-xl`) on mac |
| `main` | flex-1 | — | — | opaque `bg-background` |
| `workspace` | 360 | — | — | opaque `bg-background` |
| `fileTree` | 280 | 220 | 500 | opaque `bg-background` |
| `assistant` | 320 | 260 | 460 | opaque `bg-background` |

### `CardSurface` (`card-primitives.tsx:126-150`)
The inner surface. `flex h-full w-full flex-col overflow-hidden`. Emits
`data-platform-sidebar` / `data-workspace-sidebar` / `data-platform-main-content`
/ `data-platform-file-tree` / `data-platform-assistant` (used by platform CSS).

### `ResizeGutter` (`card-primitives.tsx:182-278`)
An **8px-wide** sibling (`RESIZE_GUTTER_WIDTH_PX = 8`) placed *between* two
CardFrames — never inside one. Hosts a 2px centered resize line. Hover paints a
cursor-following gradient. **Double-click resets** the adjacent panel to its
default width.

> There is also an older `ResizeHandle.tsx` (8px hit area, 2px line,
> negative margins) used *inside* some panels (PreviewPanel, DashboardPanel)
> for internal sub-panel widths. The shell uses `ResizeGutter`.

**WinUI:** each card = a `Border` with a `Style` setting `CornerRadius`,
`Background`, `BorderBrush`, and a subtle `Translation`/shadow. The gutter =
an 8px `ColumnDefinition` with a `Grid`/`Border` handling
`PointerPressed`/`PointerMoved`/`PointerReleased` for drag-resize and
`DoubleTapped` for reset. This is **already implemented** in
`Apps/DesktopWinUI/Views/ShellPage.xaml` (sidebar + workspace gutters wired with
full pointer handlers). See [04](04-winui-mapping.md).

---

## 6. Top bar (`UnifiedTopBar`)

Source: `src/components/layout/UnifiedTopBar.tsx`. Height `h-10` (40px),
`shrink-0`, draggable region (`WebkitAppRegion: 'drag'`).

### Non-chat routes (`UnifiedTopBar.tsx:195-217`)
A thin drag region with only the sidebar toggle button (+ Back button on
`/settings` routes).

### Chat-detail routes (`UnifiedTopBar.tsx:221-444`)
Full bar, three zones:

```
[≡ sidebar toggle] [session title ▾] [· project name ▾] ... [git branch ▾] [📁] [▣] [⚙]
└────── left (auto) ──────────────┘   └── spacer (flex-1) ──┘ └── right (auto) ──┘
```

- **Left:** sidebar toggle (clears macOS traffic lights via
  `--platform-traffic-light-safe-area: 78px`), session title → project name
  (muted, opens folder) → "..." dropdown (Split screen / Rename / Copy ID /
  Delete).
- **Spacer** (`flex-1`).
- **Right:** panel toggles — Git branch label (click → switches WorkspaceSidebar
  to Git tab), File Tree toggle, Workspace Sidebar toggle. On **Windows**,
  reserves **138px** for the native caption buttons (`titleBarOverlay`).

**WinUI:** the top bar is row 0 of `ShellPage.xaml`'s root `Grid`
(`RowDefinition Height="40"`). Left/center/right are three `ColumnDefinition`s
(Auto/*/Auto). ✅ Already implemented — see `ShellPage.xaml:17-120`.

---

## 7. Left sidebar — navigation

There are **two interchangeable left sidebars**, switched by route inside
`AppShell` (`:728-747`): `SettingsSidebar` for `/settings/*`, `ChatListPanel`
everywhere else. The old `NavRail.tsx` is dead code.

### `ChatListPanel` (chat/main routes)
Source: `src/components/layout/ChatListPanel.tsx:438-845`. Vertical flex column:

1. **Top action area** (`p-2`): New Conversation button, Search (⌘K) button,
   feature nav links (`/plugins`, `/gallery`).
2. **Scrollable section list** (`ScrollArea flex-1`): two collapsible sections —
   **项目 (Projects)** (grouped by working directory, with "New Project" entry)
   and **助理 (Assistant)** (flat list). Each project group is collapsible;
   sessions truncate at 10 (`SESSION_TRUNCATE_LIMIT = 10`). Uses `motion` for
   collapse animations.
3. **Bottom:** Settings link (`/settings`) with update badge.

Session groups auto-collapse except the most-recently-active project
(`ChatListPanel.tsx:401-421`).

### `SettingsSidebar` (`/settings/*`)
Renders `SETTINGS_NAV_ITEMS` from `src/components/settings/nav-config.ts:53-68`
— **12 vertical nav links** in order: Overview, General, Appearance, Providers,
Models, Runtime, Health, Usage, Assistant, Tasks, Bridge, About.

**WinUI:** `Apps/DesktopWinUI/Controls/Sidebar.xaml` exists (🟡 partial). The
session list item style (`rounded-xl px-3 h-8`, active = `bg-primary/12`) and
project-grouping structure still need work. See [03](03-components.md)
§"Sidebar".

---

## 8. Main content area & routing

Navigation is **Next.js App Router URL-based**. Routes (`src/app/`):

| Route | Renders |
|-------|---------|
| `/` | redirects to `/chat` (`page.tsx`) |
| `/chat` | new conversation page — centered hero (welcome + composer) |
| `/chat/[id]` | active chat view (transcript + composer + right rails) |
| `/plugins`, `/gallery`, `/extensions`, `/mcp`, `/cli-tools`, `/skills`, `/bridge`, `/design-system` | feature pages |
| `/settings/*` | 12 settings sub-pages |

The main `<main>` content swaps based on route; the shell persists. Chat-detail
routes (`/chat/[id]`) mount the right rails; other routes render only sidebar +
main.

### Split screen
Source: `src/components/layout/SplitChatContainer.tsx` + `SplitColumn.tsx`. When
`splitSessions.length >= 2`, the main `<main>` swaps to `<SplitChatContainer>`
(`AppShell.tsx:176`): a horizontal `flex h-full gap-0` row of up to 2
`SplitColumn`s, separated by a 1px `bg-border` divider. Each column is
`flex-1 min-w-0`, with `border-2 border-primary` when active. Split state
persists to localStorage (`codepilot:split-sessions`). Added via the "..." menu's
"Split screen" action.

---

## 9. Right rail panels

Two additive right-rail surfaces mount only on **chat-detail routes**.

### `PanelZone` (`src/components/layout/PanelZone.tsx:77-106`)
Mounts the lightweight independent panels, each as its own `CardFrame` +
`ResizeGutter`:

| Panel | `kind` | Width range | Default | Contents |
|-------|--------|-------------|---------|----------|
| `AssistantPanel` | `assistant` | 260–460 | 320 | Assistant summary card (name, avatar, status) |
| `FileTreePanel` | `fileTree` | 220–500 | 280 | VS-Code-style file browser: action icons row (new file/folder/refresh), optional new-item input, FileTree |

Both gated by `assistantPanelOpen` / `fileTreeOpen` in `PanelContext`.

### `WorkspaceSidebar` (`src/components/layout/WorkspaceSidebar/index.tsx`)
The unified Tab shell (width 360 default, resizable, `kind="workspace"`). State
(open/width/activeTabId/dynamicTabs) persists per-session to localStorage via
`useWorkspaceSidebar` (`src/hooks/useWorkspaceSidebar.tsx`). Renders just two
children:

```
<TabBar/>       ← top tab strip
<TabPanel/>     ← active tab content
```

**TabBar** (`WorkspaceSidebar/TabBar.tsx`): a `role="tablist"` row. Layout:
`[git] [widget] · [dynamic tabs...] · [collapse]`. Fixed tabs (git, widget) are
`shrink-0`; dynamic tabs are `flex-1 min-w-[40px] max-w-[160px]`. WAI-ARIA tabs
pattern with ArrowLeft/Right/Home/End keyboard nav. Close button folds into the
leading icon (hover swaps file icon → X). Collapse button pinned at far right.

**TabPanel** (`WorkspaceSidebar/TabPanel.tsx:41-58`) routes the active tab:

| Tab id | Content |
|--------|---------|
| `git` (fixed) | `GitTabContent` (`panels/GitPanel.tsx`) — refresh button + Git panel |
| `widget` (fixed) | `WidgetTabContent` / `DashboardPanel` (`panels/DashboardPanel.tsx`) — widget grid + assistant summary |
| `files-pinned` | `FileTreePanel variant="sidebar"` |
| `markdown` / `file` / `artifact` | `PreviewPanel` (`panels/PreviewPanel.tsx`, 2000+ lines) — file/code/markdown/HTML/diff/JSON/datatable preview with Source/Rendered/Edit view modes, autosave |

Tabs open via a `workspace-tab-open-request` window event (decoupling);
`setPreviewSource` in `AppShell` dispatches this event (`:570-574`).

**WinUI:** the workspace column already exists in `ShellPage.xaml:167-425` with
Git/Progress/Files tab buttons and stub content (🟡). The independent
`PanelZone` (assistant + file-tree as separate cards) is **not yet** wired as
additive — currently the workspace column holds the file tree inline. See
[04](04-winui-mapping.md) §"WinUI state inventory".

---

## 10. Persistence model

All panel state persists to **localStorage** (browser/Electron), keyed per
session or globally:

| Key | Scope | Value |
|-----|-------|-------|
| `codepilot:split-sessions` | global | split session ids |
| `codepilot:last-model`, `codepilot:last-provider-id` | global | last-used model + provider |
| per-session workspace sidebar | per session | `{open, width, activeTabId, dynamicTabs}` |
| collapsed projects | global | set of project keys |

**WinUI:** replace localStorage with `ApplicationData.Current.LocalSettings`
(app-wide) or a per-session JSON in the session directory (the CodeHarness
`codeharness::session` module already owns `<root>/<workdir-key>/<sessionId>/`).
The window-level width/state belongs in app settings; the per-chat workspace
tabs belong alongside `state.json`.
