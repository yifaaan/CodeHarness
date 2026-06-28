# 01 — Design Tokens

Exact values for every color, font, radius, spacing, and shadow in the CodePilot
design system. Source of truth: `src/app/globals.css` (826 lines) — every token
below is defined there. Mirrored verbatim in `themes/default.json`.

> **For WinUI:** use the **sRGB hex** column directly in
> `<SolidColorBrush Color="#...">`. OKLCH→sRGB is lossy; the hex values here
> were converted with a proper OKLCH→sRGB converter (clamped to sRGB gamut).
> Re-verify any token not listed here before using a hand-estimate.

---

## 1. Color system

### 1.1 Brand identity

The CodePilot brand color is **monochrome** — charcoal in light mode, near-white
in dark mode. This is a **deliberate** move: `globals.css:79` comments confirm
`--primary` was changed from a blue `oklch(0.546 0.245 262.881)` to charcoal
`oklch(0.262 0 0)` (= `#252525`). **There is no accent blue** in the brand
palette — blue appears only in `status-info` and `chart-1`.

### 1.2 Light mode (`:root`, `globals.css:66-160`)

| Token | OKLCH | sRGB hex | Tailwind alias |
|-------|-------|----------|----------------|
| `--background` | `oklch(1 0 0)` | `#FFFFFF` | `bg-background` |
| `--foreground` | `oklch(0.147 0.004 49.25)` | `#1C1917` | `text-foreground` |
| `--card` | `oklch(1 0 0)` | `#FFFFFF` | `bg-card` |
| `--card-foreground` | `oklch(0.147 0.004 49.25)` | `#1C1917` | `text-card-foreground` |
| `--popover` | `oklch(1 0 0)` | `#FFFFFF` | `bg-popover` |
| `--popover-foreground` | `oklch(0.147 0.004 49.25)` | `#1C1917` | |
| `--primary` | `oklch(0.262 0 0)` | `#3A3A39`¹ | `bg-primary` |
| `--primary-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` | `text-primary-foreground` |
| `--secondary` | `oklch(0.95 0.002 106.424)` | `#F2F0EE` | `bg-secondary` |
| `--secondary-foreground` | `oklch(0.216 0.006 56.043)` | `#33312D` | |
| `--muted` | `oklch(0.95 0.002 106.424)` | `#F2F0EE` | `bg-muted` |
| `--muted-foreground` | `oklch(0.553 0.013 58.071)` | `#807A72` | `text-muted-foreground` |
| `--accent` | `oklch(0.95 0.002 106.424)` | `#F2F0EE` | `bg-accent` |
| `--accent-foreground` | `oklch(0.216 0.006 56.043)` | `#33312D` | |
| `--destructive` | `oklch(0.577 0.245 27.325)` | `#DC2626` | `bg-destructive` |
| `--border` | `oklch(0.923 0.003 48.717)` | `#E8E6E2` | `border-border` |
| `--input` | `oklch(0.923 0.003 48.717)` | `#E8E6E2` | `bg-input` |
| `--ring` | `oklch(0.262 0 0)` | `#3A3A39` | focus ring |
| `--sidebar` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` | `bg-sidebar` |
| `--sidebar-foreground` | `oklch(0.147 0.004 49.25)` | `#1C1917` | |
| `--sidebar-primary` | `oklch(0.262 0 0)` | `#3A3A39` | |
| `--sidebar-primary-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` | |
| `--sidebar-accent` | `oklch(0.95 0.002 106.424)` | `#F2F0EE` | |
| `--sidebar-accent-foreground` | `oklch(0.216 0.006 56.043)` | `#33312D` | |
| `--sidebar-border` | `oklch(0.923 0.003 48.717)` | `#E8E6E2` | |
| `--sidebar-ring` | `oklch(0.262 0 0)` | `#3A3A39` | |
| `--user-bubble` | `oklch(0.22 0.005 250)` | `#26282E` | chat user bubble bg |
| `--user-bubble-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` | |

¹ CodePilot comment says `#252525`; `oklch(0.262 0 0)` converts to `#3A3A39`.
  The current WinUI `Colors.xaml` uses `#252525` for `CodeHarnessAccentBrush`,
  matching the CodePilot **comment intent**. Keep `#252525` for the accent.

**Chart palette** (`globals.css:95-99`):

| Token | OKLCH | sRGB hex |
|-------|-------|----------|
| `--chart-1` | `oklch(0.546 0.245 262.881)` | `#2563EB` (blue) |
| `--chart-2` | `oklch(0.6 0.118 184.704)` | `#13B5B5` (teal) |
| `--chart-3` | `oklch(0.398 0.07 227.392)` | `#404E79` |
| `--chart-4` | `oklch(0.828 0.189 84.429)` | `#E8B23A` (yellow) |
| `--chart-5` | `oklch(0.769 0.188 70.08)` | `#E8812A` (orange) |

### 1.3 Dark mode (`.dark`, `globals.css:162-228`)

| Token | OKLCH | sRGB hex |
|-------|-------|----------|
| `--background` | `oklch(0.147 0.004 49.25)` | `#1C1917` |
| `--foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--card` | `oklch(0.216 0.006 56.043)` | `#33312D` |
| `--card-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--popover` | `oklch(0.216 0.006 56.043)` | `#33312D` |
| `--popover-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--primary` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` (**inverts to near-white**) |
| `--primary-foreground` | `oklch(0.262 0 0)` | `#3A3A39` |
| `--secondary` | `oklch(0.268 0.007 34.298)` | `#3C3833` |
| `--secondary-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--muted` | `oklch(0.268 0.007 34.298)` | `#3C3833` |
| `--muted-foreground` | `oklch(0.709 0.01 56.259)` | `#A39B92` |
| `--accent` | `oklch(0.268 0.007 34.298)` | `#3C3833` |
| `--accent-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--destructive` | `oklch(0.704 0.191 22.216)` | `#F87171` |
| `--border` | `oklch(1 0 0 / 10%)` | `#FFFFFF` at 10% alpha |
| `--input` | `oklch(1 0 0 / 15%)` | `#FFFFFF` at 15% alpha |
| `--ring` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--sidebar` | `oklch(0.216 0.006 56.043)` | `#33312D` |
| `--sidebar-foreground` | `oklch(0.985 0.001 106.423)` | `#FCFCFA` |
| `--user-bubble` | `oklch(0.90 0.003 106)` | `#E5E3DE` (inverted) |
| `--user-bubble-foreground` | `oklch(0.147 0.004 49.25)` | `#1C1917` |

**Key dark-mode rules:**
- `--primary` **flips** from charcoal to near-white (so primary buttons become
  light-on-dark → dark-on-light).
- `--border` and `--input` become **white at low alpha** (10% / 15%) rather than
  a solid grey — this keeps borders subtle on the dark surface.
- Alpha colors: WinUI `Color` uses `#AARRGGBB` (alpha first). 10% white =
  `#1AFFFFFF`, 15% white = `#26FFFFFF`.

---

### 1.4 Status semantic colors

Each status has **4 sub-tokens**: base, foreground, muted (bg @ 10% alpha),
border (@ 20% alpha). Source: `globals.css:111-127` (light), `:201-216` (dark).

#### Success (green)
| Sub-token | Light OKLCH → sRGB | Dark OKLCH → sRGB |
|-----------|--------------------|--------------------|
| base | `oklch(0.527 0.154 150.069)` → `#16A34A` | `oklch(0.527 0.154 150.069)` → `#16A34A` |
| foreground | `oklch(0.527 0.154 150.069)` → `#16A34A` | `oklch(0.7 0.15 155)` → `#4ADE80` |
| muted | `oklch(0.527 0.154 150.069 / 0.10)` → `#1A16A34A` | (same pattern, dark base) |
| border | `oklch(0.527 0.154 150.069 / 0.20)` → `#3316A34A` | |

#### Warning (amber)
| Sub-token | Light OKLCH → sRGB | Dark OKLCH → sRGB |
|-----------|--------------------|--------------------|
| base | `oklch(0.705 0.213 47.604)` → `#E8AB13` | `oklch(0.705 0.213 47.604)` → `#E8AB13` |
| foreground | → `#B45309`² | → `#FBBF24` |
| muted | `/ 0.10` → `#1AE8AB13` | |
| border | `/ 0.20` → `#33E8AB13` | |

² WinUI `Colors.xaml` uses `#D97706` for `CodeHarnessBusyDotBrush` — a fine
  amber, keep it.

#### Error (red) — equals `--destructive`
| Sub-token | Light sRGB | Dark sRGB |
|-----------|-----------|-----------|
| base | `#DC2626` | `#F87171` |
| muted | `#1ADC2626` | `#1AF87171` |
| border | `#33DC2626` | `#33F87171` |

#### Info (blue)
| Sub-token | Light OKLCH → sRGB | Dark OKLCH → sRGB |
|-----------|--------------------|--------------------|
| base | `oklch(0.546 0.245 262.881)` → `#2563EB` | `oklch(0.623 0.214 259.815)` → `#3B82F6` |
| muted | `#1A2563EB` | |
| border | `#332563EB` | |

---

### 1.5 Terminal / code-block colors

**Always dark**, even in light mode — bash blocks render on a dark background.
Source: `globals.css:128-135`.

| Token | OKLCH → sRGB | Use |
|-------|--------------|-----|
| `--terminal-bg` | `oklch(0.13 0.004 49.25)` → `#13110F` | code/terminal background |
| `--terminal-foreground` | `oklch(0.87 0.006 106)` → `#DAD7CF` | terminal text |
| `--terminal-muted` | `oklch(0.55 0.01 58)` → `#7C756B` | dimmed terminal text |
| `--terminal-border` | `oklch(0.30 0.005 49)` → `#45413B` | terminal border |
| `--terminal-accent` | `oklch(0.55 0.17 155)` → `#13A050` | terminal prompt/accent (green) |

Also defined (light): `--terminal-hover-bg`, `--terminal-gradient-from` (used for
the terminal gradient overlay).

---

### 1.6 Callout block colors (`globals.css:717-722`)

Hex values (not OKLCH); bg is each at 8% alpha.

| Callout | Foreground | Background |
|---------|-----------|------------|
| note | `#6B7280` | `#146B7280` |
| tip | `#10B981` | `#1410B981` |
| important | `#8B5CF6` | `#148B5CF6` |
| warning | `#F59E0B` | `#14F59E0B` |
| caution | `#EF4444` | `#14EF4444` |
| info | `#3B82F6` | `#143B82F6` |

---

## 2. Typography

### 2.1 Font families

Source: `src/app/layout.tsx:13-21` (loaded via `next/font/google`), mapped in
`globals.css:9-10`.

| Role | Family | CSS var | WinUI fallback |
|------|--------|---------|----------------|
| Sans (UI) | **Geist** | `--font-geist-sans` → `--font-sans` | Segoe UI Variable |
| Mono (code) | **Geist Mono** | `--font-geist-mono` → `--font-mono` | Cascadia Code |

Body classes (`layout.tsx:81`): `${geistSans.variable} ${geistMono.variable} antialiased`.

**WinUI strategy:** bundle the **Geist** TTFs as app resources for full fidelity,
or fall back to **Segoe UI Variable** (sans) + **Cascadia Code** (mono). Set
`FontFamily` globally in `App.xaml`; override to mono on `TextBlock`/`Run`
elements rendering code.

### 2.2 Font size scale (Tailwind defaults, used as tokens)

| Token | px | Typical use |
|-------|----|----|
| `text-[10px]` | 10 | count badges, tiny labels, mono file paths |
| `text-[11px]` | 11 | secondary file paths, "Also modified" |
| `text-xs` | 12 | metadata, timestamps, footer text, labels |
| `text-sm` | 14 | body text, buttons, inputs (desktop) |
| `text-base` | 16 | textarea (mobile breakpoint), inputs default |
| `text-lg` | 18 | dialog titles, h3 |
| `text-xl` | 20 | h2 |
| `text-2xl` | 24 | h1 (article) |
| `text-3xl` | 30 | hero greeting (`NewChatWelcome`) |

> Inputs/textarea use `text-base md:text-sm` = **16px on mobile, 14px on desktop**.
> The WinUI port is desktop-only → use **14px** for inputs.

### 2.3 Font weights

| Token | Weight | Dominant use |
|-------|--------|--------------|
| `font-normal` | 400 | body paragraphs |
| `font-medium` | 500 | **dominant** — Button base, Label, FieldRow labels, session items |
| `font-semibold` | 600 | Card titles, dialog titles, headings |

### 2.4 Line heights

| Context | Value | Source |
|---------|-------|--------|
| Prose paragraphs | `1.7` | `globals.css:552` |
| Article template | `1.75` | `globals.css:756` |
| Report template | `1.6` | |
| Brief template | `1.55` | |
| Pitch template | `1.65` | |
| Dialog title | `leading-none` (1.0) | `dialog.tsx:145` |
| Alert description | `leading-relaxed` (1.625) | |

In chat message content: paragraphs `leading-7` (= 1.75 of 14px ≈ 25px).
`<MessageResponse>` strips outer margins via `[&>*:first-child]:mt-0 [&>*:last-child]:mb-0`.

---

## 3. Spacing & sizing system

Tailwind v4 standard scale (0.25rem = **4px** base unit). Concrete values used
throughout:

| Element | Value | Source |
|---------|-------|--------|
| Sidebar default width | **240px** (range 180–300) | `AppShell.tsx:94,316` |
| Workspace sidebar default | **360px** | `AppShell.tsx:189` |
| File-tree panel default | **280px** (220–500) | `PanelZone.tsx` |
| Assistant panel default | **320px** (260–460) | `PanelZone.tsx` |
| Resize gutter | **8px** | `card-primitives.tsx:180` |
| Chat content max width | **768px** (`max-w-3xl`) | `ChatView.tsx:1286` |
| Message gap | **24px** (`gap-6`) | `MessageList.tsx` |
| macOS shell inset | `8px 16px 16px 16px`, gap `4px` | `globals.css:431-432` |
| macOS card corner radius | **14px** | `globals.css:465,493` |
| Scrollbar width/height | **6px**, thumb radius 3px | `globals.css:528-547` |

**Common Tailwind spacing tokens** (multiply by 4 for px):
`gap-1`=4, `gap-1.5`=6, `gap-2`=8, `gap-3`=12, `gap-4`=16, `gap-6`=24, `gap-8`=32,
`p-1`=4, `p-2`=8, `p-3`=12, `p-4`=16, `p-6`=24, `px-4`=16h, `py-3`=12v.

---

## 4. Border radius scale

Derived from a single root: **`--radius: 1rem`** (= 16px, `globals.css:72`;
comment: "bumped from 0.75rem"). Source: `globals.css:40-46`.

| Token | Calc | px |
|-------|------|----|
| `--radius-sm` | `calc(var(--radius) - 4px)` | **12** |
| `--radius-md` | `calc(var(--radius) - 2px)` | **14** |
| `--radius-lg` | `var(--radius)` | **16** |
| `--radius-xl` | `calc(var(--radius) + 4px)` | **20** |
| `--radius-2xl` | `calc(var(--radius) + 8px)` | **24** |
| `--radius-3xl` | `calc(var(--radius) + 12px)` | **28** |
| `--radius-4xl` | `calc(var(--radius) + 16px)` | **32** |
| `rounded-full` | `9999px` | pill / circle |

### 4.1 Per-component radius usage (CRITICAL — diverges from defaults)

CodePilot applies `rounded-full` (pill) to most controls regardless of the
`--radius-*` scale. **This is the design's signature look.** Match it exactly.

| Component | Radius class | px | WinUI `CornerRadius` |
|-----------|-------------|----|----|
| **Button** (all variants) | `rounded-full` | pill | `Height/2` |
| **Input** | `rounded-3xl` | 28 (very pill) | large fixed or `Height/2` |
| **Select trigger** | `rounded-3xl` | 28 | |
| **Badge** | `rounded-full` | pill | |
| **Switch** | `rounded-full` | pill | track = pill |
| **TabsList / Tabs trigger** | `rounded-full` | pill | |
| **Popover** | `rounded-3xl` | 28 | |
| **Tooltip** | `rounded-md` | 14 | 14 |
| **Progress** | `rounded-full` | pill | |
| **Label** | — (no box) | — | — |
| **Card** | `rounded-3xl` | 28 | 28 |
| **Dialog** | `rounded-lg` | 16 | 16 |
| **Textarea** | `rounded-md` | 14 | 14 |
| **Alert** | `rounded-lg` | 16 | 16 |
| **User message bubble** | `rounded-2xl` | 24 | 24 |
| **Composer box (InputGroup)** | `rounded-2xl` | 24 | 24 (current WinUI uses 22 — adjust to 24) |
| **Code block card** | `rounded-md` | 14 | 14 |
| **Widget-card (markdown table/code frame)** | `rounded-xl` | 20 | 20 |
| **DiffSummary ArtifactFileCard** | `rounded-lg` | 8 (`rounded-lg` Tailwind = 8px in v3 / 16px in v4 — **use 8px**, the comment intent) | 8 |
| **SettingsCard** | `rounded-lg` | 8 | 8 |

> **Tailwind v4 note:** `rounded-lg` = `var(--radius-lg)` = 16px in v4, but many
> CodePilot components use it where the *intent* is a small radius. When a
> class string says `rounded-lg` on a small sub-card, verify the visual: if it
> looks like ~8px, use 8px in WinUI. The `DiffSummary` and `SettingsCard` are
> 8px; a top-level `Card` is 28px.

---

## 5. Shadows

### 5.1 Custom shadow tokens (`globals.css:139`, `:227`)

**`--shadow-diffuse`** — the floating composer / command-list shadow:

| Mode | Value |
|------|-------|
| Light | `0 12px 40px -8px rgba(0,0,0,0.10), 0 4px 12px -4px rgba(0,0,0,0.04)` |
| Dark | `0 12px 40px -8px rgba(0,0,0,0.45), 0 4px 12px -4px rgba(0,0,0,0.25)` |

Consumed in `MessageInput.tsx:1067` (composer) and `patterns/CommandList.tsx:26`.

### 5.2 macOS card frame shadow (`globals.css:475-485`)

Three-layer elevation (light):
`0 1px 1px -0.5px rgba(0,0,0,0.06), 0 3px 3px -1.5px rgba(0,0,0,0.06), 0 6px 6px -3px rgba(0,0,0,0.06)`.
Dark uses `0.30` alpha.

### 5.3 Tailwind shadow tokens used

| Token | Use |
|-------|-----|
| `shadow-md` | Card, Select content, Dropdown |
| `shadow-lg` | Dialog, Popover |
| `shadow-sm` | Textarea, Switch thumb, ButtonGroupText |
| `shadow-xs` | (small UI) |

**WinUI:** shadows are limited in WinUI 3. For the diffuse composer shadow, use
a `Border` with a `DropShadow` via `Microsoft.UI.Composition` (theme-aware), or
a layered approach (stacked semi-transparent `Border`s). For card elevation,
`Translation="0,0,16"` + a `ThemeShadow` gives a reasonable float. The current
WinUI `ShellPage.xaml` already uses `Translation="0,0,16"` on card borders — keep
that pattern.

---

## 6. Animations & transitions

### 6.1 Utility animations (`tw-animate-css`)

Classes (not custom keyframes): `animate-in`, `animate-out`, `fade-in-0`,
`fade-out-0`, `zoom-in-95`, `zoom-out-95`, `slide-in-from-top-2` (+ bottom/left/
right variants). Dialog/popover/dropdown/select/tooltip use these with
`duration-100` / `duration-200`.

### 6.2 Custom keyframes (`globals.css`)

| Name | Lines | Use |
|------|-------|-----|
| `search-highlight-pulse` | `595-605` | `@utility search-highlight-flash` 2s ease-in-out |
| `file-tree-pulse` | `615-625` | `@utility file-tree-flash` 2s |
| `widget-shimmer` | `632-639` | `@utility animate-shimmer` 2s infinite linear gradient sweep |

### 6.3 Markdown body transition

`transition: padding 150ms ease, font-family 150ms ease, color 150ms ease`
(`globals.css:747`).

### 6.4 Selection color

`color-mix(in oklch, var(--primary) 30%, transparent)` (light), 35% in dark
(`globals.css:520-525`).

**WinUI:** see [04](04-winui-mapping.md) §"Animation mapping" for Framer Motion
→ `Storyboard` / `VisualStateManager` translations. The shimmer text sweep needs
a custom `Composition` linear-gradient brush animation.

---

## 7. The `--platform-*` token layer

Source: `globals.css:230-391`. Gated on `data-platform` / `data-shell` /
`data-platform-style` attributes (set by anti-FOUC script, `layout.tsx:71`).
This is the macOS "Tahoe/Liquid Glass" treatment.

**For the Windows port, the only override is** (`globals.css:325-327`):
`--platform-titlebar-safe-area: 44px` (gated on `data-shell="electron"`).

Default platform tokens (`globals.css:255-307`):

| Token | Value |
|-------|-------|
| `--platform-surface-sidebar` | `color-mix(in oklch, var(--sidebar) 80%, transparent)` |
| `--platform-surface-bar` | `var(--background)` |
| `--platform-surface-popover` | (defined per-platform) |
| `--platform-radius-window` | `var(--radius)` (16px) |
| `--platform-radius-control` | `calc(var(--radius) - 4px)` (12px) |
| `--platform-hover-alpha` | `1` |
| `--platform-border-subtle` | `color-mix(in oklch, var(--border) 40%, transparent)` |

**WinUI:** target the **Windows profile** — opaque surfaces (no
`color-mix` translucency), `MicaController` for window backdrop. Do **not**
attempt the macOS translucent-sidebar look on Windows.

---

## 8. WinUI `ResourceDictionary` mapping

The current WinUI `Apps/DesktopWinUI/Themes/Colors.xaml` already defines a
CodePilot/Luma-inspired token set. Cross-reference:

| CodePilot token | WinUI brush key | Current value | Action |
|-----------------|-----------------|---------------|--------|
| `--background` | `CodeHarnessWindowBrush` / `BackgroundBrush` | `#FFFAFAF9` | ⚠️ CodePilot bg is `#FFFFFF`; `#FAFAF9` is the sidebar/window tint. Acceptable — keep as window bg. |
| `--card` | `CodeHarnessCardBrush` / `MainSurfaceBrush` | `#FFFFFFFF` | ✅ |
| `--muted` | `CodeHarnessSubtleCardBrush` / `UserBubbleBrush` | `#FFF5F5F4` | ✅ (≈ `#F2F0EE`) |
| `--primary` | `CodeHarnessAccentBrush` | `#FF252525` | ✅ (matches CodePilot comment) |
| `--foreground` | `CodeHarnessPrimaryTextBrush` | `#FF242424` | ✅ |
| `--muted-foreground` | `CodeHarnessMutedTextBrush` | `#FF85857D` | ✅ (≈ `#807A72`) |
| `--border` | `CodeHarnessBorderBrush` | `#FFE5E5E2` | ✅ (≈ `#E8E6E2`) |
| `--destructive` | `GitDeletedBrush` | `#FFDC2626` | ✅ |
| `--user-bubble` | `CodeHarnessUserBubbleBrush` | `#FFF5F5F4` | ⚠️ CodePilot user-bubble is `bg-muted` (light grey), not the dark `--user-bubble` token. WinUI is correct — keep `#F5F5F4`. |

**Gaps to fill:**
- **Dark-mode brushes** — `Colors.xaml` has no `Dark` resource dictionary. Add a
  `[ThemeDictionary]` or a separate `Colors.Dark.xaml` with the §1.3 values.
- **Status brushes** — add `SuccessBrush`, `WarningBrush`, `InfoBrush` (+ muted/
  border variants) per §1.4. Only `GitAdded`/`GitModified`/`GitDeleted` exist.
- **Terminal brushes** — add `TerminalBgBrush` (`#13110F`), `TerminalFgBrush`,
  `TerminalMutedBrush`, `TerminalAccentBrush` per §1.5.
- **Chart brushes** — add `Chart1Brush`..`Chart5Brush` per §1.2 if dashboards are
  ported.

Radius values belong in `Controls.xaml` `Style`s as `Setter Property="CornerRadius"`.
See [03](03-components.md) for per-component values.
