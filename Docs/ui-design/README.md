# CodePilot UI Design Specification

A 1:1 visual + structural reference for porting the **CodePilot** desktop UI into
the **CodeHarness WinUI 3 (C++)** application.

This is the single source of truth for *what the UI must look and behave like*.
It captures the CodePilot React/Electron design in enough detail to reproduce it
pixel-for-pixel in XAML, and it cross-references the **current** state of
`Apps/DesktopWinUI/` so the porting work is grounded in what already exists.

---

## What is CodePilot?

CodePilot is a **Next.js 16 (App Router) + Electron 40** multi-model AI agent
desktop client (v0.56.0). Its visual language is a **macOS Tahoe / "Liquid
Glass"-inspired floating-card layout** with a deliberately **monochrome** brand
identity (charcoal in light mode, near-white in dark mode — no accent blue).

We replicate only the **visual UI**, not its backend / API / IPC layer. The
CodeHarness C++ core (`codeharness::agent`, `codeharness::session`, ...) already
provides the runtime; this spec describes the shell that wraps it.

---

## Document Index

| # | Document | What it covers |
|---|----------|----------------|
| — | **README.md** *(this file)* | Index, how to use, conversion caveats, current-state summary |
| 00 | [00-overview.md](00-overview.md) | Tech stack, window chrome, hierarchical layout, card primitives, navigation model |
| 01 | [01-design-tokens.md](01-design-tokens.md) | Exact OKLCH colors (light+dark), Geist typography, radius scale, shadow tokens, OKLCH→sRGB hex table |
| 02 | [02-chat-content.md](02-chat-content.md) | Transcript, user/assistant messages, tool-call rows, thinking blocks, code blocks, markdown, diff cards, streaming UI |
| 03 | [03-components.md](03-components.md) | Button / Card / Input / Composer / Sidebar / TopBar / WorkspaceSidebar / right-rail panels / shared patterns |
| 04 | [04-winui-mapping.md](04-winui-mapping.md) | TSX→XAML translation tables, icon strategy, animation mapping, **current WinUI state inventory + prioritized gap checklist** |

> Read in order **00 → 01 → 02 → 03 → 04** the first time. 04 is the
> "where am I / what's next" index once you're in implementation.

---

## How to use this spec

1. **Start a feature** → read the relevant content doc (02 for chat, 03 for a
   primitive, 00 for layout).
2. **Pick exact values** from 01 (tokens). Do not eyeball colors or radii.
3. **Translate to XAML** using the mapping tables in 04.
4. **Check current state** in 04's "WinUI state inventory" before building —
   many shell pieces already exist and only need refinement.

---

## Source of truth

All claims in this spec cite a CodePilot source path + line number, e.g.
`globals.css:79`, `AppShell.tsx:94`. The CodePilot tree lives at:

```
D:/code clone/CodePilot/
├── src/app/                     # Next.js routes + globals.css (the design system)
│   ├── layout.tsx               # root layout, providers, anti-FOUC theme attrs
│   └── globals.css              # ★ ALL design tokens live here (826 lines)
├── src/components/
│   ├── layout/                  # AppShell, TopBar, sidebars, panels, card primitives
│   ├── chat/                    # ChatView, MessageList, MessageItem, DiffSummary, ...
│   ├── ai-elements/             # generic message/tool/code/reasoning primitives
│   ├── ui/                      # shadcn/ui primitives (button, card, input, ...)
│   └── patterns/                # SettingsCard, FieldRow, StatusBanner, ...
├── electron/main.ts             # window creation (size, titlebar, vibrancy)
└── components.json              # shadcn config (style: "radix-luma", baseColor: "stone")
```

The single most important file in the entire design system is
**`src/app/globals.css`** — every color, radius, shadow, and platform token is
defined there. When in doubt, grep it.

---

## Conversion caveats (read once)

| Concern | Reality | Mitigation |
|---------|---------|------------|
| **OKLCH → sRGB is lossy** | CodePilot colors are `oklch(L C H)`. WinUI `Color` is sRGB. Out-of-gamut OKLCH values clamp. | 01 provides a pre-computed sRGB hex table for the ~30 load-bearing tokens. Use it. For others, use a proper OKLCH→sRGB converter (not a mental estimate). |
| **Tailwind ≠ XAML** | Tailwind is utility classes on a flex/grid CSS box model; XAML is a retained object tree with `Grid`/`StackPanel`/`Border`. | 04 has a class-by-class translation table. `flex` → `StackPanel` or `Grid` with `ColumnDefinition`/`RowDefinition`; `gap-N` → `Spacing="N*4"`; `max-w-3xl` → `MaxWidth="768"`. |
| **`rounded-full` ≠ circle** | Tailwind `rounded-full` = `border-radius: 9999px`, which renders as a **pill** (not a circle) on non-square elements. | In XAML use `CornerRadius` = `Height/2` for a true pill; use a fixed large value only for square icon buttons. |
| **Backdrop blur / vibrancy** | CodePilot uses `backdrop-blur-xl` + translucent sidebar surfaces on macOS. | On Windows there is no NSWindow vibrancy. WinUI uses `MicaController` / `DesktopAcrylicController` for the window background, and opaque surfaces for cards. See 00 §"Platform profile on Windows". |
| **Icons** | CodePilot uses HugeIcons (primary) + Phosphor (fallback) — two SVG icon sets. WinUI has no equivalent. | The current WinUI app uses **Segoe MDL2 Assets** glyph codepoints. 04 recommends migrating to **Segoe Fluent Icons** and provides a semantic-name → codepoint mapping table. |
| **Markdown rendering** | CodePilot uses `streamdown` + Shiki. WinUI has no built-in markdown. | 02 specifies the visual target; 04 lists implementation options (`RichTextBlock` + custom parser, or a markdown-to-XAML library). |
| **Animation** | CodePilot uses Framer Motion (height tweens) + Radix Collapsible + `tw-animate-css`. | WinUI: `Storyboard` with `DoubleAnimation` on `Height`/`Opacity`, plus `VisualStateManager` for open/closed states. 04 maps each. |
| **Font** | CodePilot uses **Geist** + **Geist Mono** (Google Fonts via `next/font`). | Geist is not a Windows system font. Either bundle the Geist TTFs as app resources, or fall back to **Segoe UI Variable** (sans) + **Cascadia Code** (mono). 01 lists both options. |

---

## Current WinUI state — at a glance

The WinUI app at `Apps/DesktopWinUI/` already mirrors the CodePilot **shell +
color tokens + layout skeleton**. The **chat content layer is largely empty**.

| Layer | CodePilot | Current WinUI | Status |
|-------|-----------|---------------|--------|
| Window shell (top bar + columns) | `AppShell.tsx` | `Views/ShellPage.xaml` | ✅ Faithful |
| Color tokens | `globals.css` | `Themes/Colors.xaml` | ✅ Faithful (light only; dark needs completion) |
| Control styles | `ui/*.tsx` | `Themes/Controls.xaml` + `Typography.xaml` | 🟡 Partial |
| Floating-card surfaces | `card-primitives.tsx` | `ShellPage.xaml` `Border` styles | ✅ Faithful |
| Top bar | `UnifiedTopBar.tsx` | `ShellPage.xaml` row 0 | 🟡 Partial (git/files toggles stubbed) |
| Sidebar (session list) | `ChatListPanel.tsx` | `Controls/Sidebar.xaml` | 🟡 Partial |
| Composer | `MessageInput.tsx` | `Controls/ComposerBox.xaml` | 🟡 Partial (no chip rows, no real model menu) |
| Chat transcript container | `MessageList.tsx` | `Views/ChatPage.xaml` | 🟡 Container only |
| **User message bubble** | `MessageItem.tsx` | code-behind built `Border` | 🟡 Simple text only |
| **Assistant message (markdown)** | `MessageResponse` + `streamdown` | ❌ Plain `TextBlock` | ❌ **Missing** |
| **Tool-call rows** | `tool-actions-group.tsx` | `Controls/ToolCallView.xaml` (empty `<Grid/>`) | ❌ **Missing** |
| **Thinking blocks** | `ThinkingRow` | — | ❌ **Missing** |
| **Code blocks (Shiki)** | `code-block.tsx` | — | ❌ **Missing** |
| **Diff/file-change cards** | `DiffSummary.tsx` | — | ❌ **Missing** |
| **Streaming shimmer** | `StreamingMessage.tsx` | — | ❌ **Missing** |
| Workspace right rail | `WorkspaceSidebar/` | `ShellPage.xaml` workspace col | 🟡 Tabs stubbed, content thin |
| Settings pages | `settings/*.tsx` | `Views/SettingsPage.xaml` | 🟡 Skeleton |

See [04-winui-mapping.md](04-winui-mapping.md) §"WinUI state inventory" for the
file-by-file breakdown and a prioritized build checklist.

---

## Conventions used in this spec

- **`code`** — exact identifier / class / token name (verbatim from CodePilot).
- **`path:line`** — clickable source reference.
- ✅ / 🟡 / ❌ — current WinUI fidelity (done / partial / missing).
- **"WinUI:"** callouts — the XAML/C++ translation, cross-referenced to
  `Apps/DesktopWinUI/` files.
- Tables with **OKLCH** and **sRGB** columns — copy the sRGB hex straight into
  `<SolidColorBrush Color="#...">`.
