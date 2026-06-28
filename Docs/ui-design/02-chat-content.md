# 02 — Chat Content (Transcript & Message Rendering)

The chat transcript is the heart of the app. This document specifies every
element that appears in the conversation area: the transcript container, user vs
assistant messages, tool-call rows, thinking blocks, code blocks, markdown
styling, diff cards, media preview, and the streaming UI.

> **Sources:** `src/components/chat/ChatView.tsx`, `MessageList.tsx`,
> `MessageItem.tsx`, `ai-elements/tool-actions-group.tsx`, `code-block.tsx`,
> `reasoning.tsx`, `chat/markdown-components.tsx`, `DiffSummary.tsx`,
> `MediaPreview.tsx`, `StreamingMessage.tsx`, `chat/NewChatWelcome.tsx`.
>
> **Current WinUI state:** `Apps/DesktopWinUI/Views/ChatPage.xaml` has the
> container + empty state only. `Controls/ToolCallView.xaml` is an empty
> `<Grid/>`. **Almost everything in this doc is not yet implemented.**

---

## 1. The chat view container (`ChatView`)

Source: `src/components/chat/ChatView.tsx:1263-1620`. Full-height vertical flex:

```
<div className="flex h-full min-h-0 flex-col">
```

Top-down stacking:
1. Optional **workspace-mismatch banner** (warning tint):
   `border-b border-status-warning/30 bg-status-warning-muted px-4 py-2`, 12px text.
2. **Two layouts**, decided by `isNewChat`:
   - **New-chat (empty) state** (`:1279-1350`): vertically + horizontally centered
     `max-w-3xl` block: `<NewChatWelcome>` → `<MessageInput>` →
     `<ChatComposerActionBar>`. Wrapped in
     `flex-1 overflow-y-auto flex flex-col items-center justify-center px-4 py-8`.
   - **Conversation state** (`:1351+`): `<MessageList>` (flex-1 scroll), then a
     stack of optional post-turn affordances in order:
     `TerminalReasonChip`, `PermissionPrompt`, skill-nudge banner,
     `BatchExecutionDashboard`, queued-message pill, `RateLimitBanner`,
     `RunCheckpoint`, `TaskCheckpoint`, error/warning banners, and finally
     `<MessageInput>` + `<ChatComposerActionBar>` + `<RunCockpit>` at the bottom.

Every `max-w-3xl` centered block uses `mx-auto w-full max-w-3xl`. The composer's
outer wrapper has a translucent vibrancy surface:
`bg-[var(--platform-surface-bar)] backdrop-blur-lg px-4 pt-2 pb-1`.

---

## 2. The message list (scrolling transcript)

Source: `src/components/chat/MessageList.tsx`.

| Property | Value |
|----------|-------|
| Scroll engine | `use-stick-to-bottom` library, wrapped in `<Conversation>` (`StickToBottom`, `initial="smooth"`, `resize="instant"`, `role="log"`) |
| Auto-follow | `ScrollOnStream` helper calls `scrollToBottom()` when streaming begins or `messageCount` increases |
| Position restore | Anchor-based (`anchorIdRef` → `scrollIntoView`) preserves position when older messages prepend via "Load earlier" |
| Content container | `mx-auto max-w-3xl px-4 py-6 gap-6` (768px wide, 24px message gap) |
| Load-more button | Top — ghost `size="sm"`, muted text, centered |
| Per-message wrapper | `<div className="group">` with `id={msg-${id}}` (anchor), optional leading `<TaskRunMarker>`, trailing `<RewindButton>` (ghost, `text-[10px]`, hover-reveal `opacity-0 group-hover:opacity-100`) |

### Scroll-to-bottom button
`ConversationScrollButton` — circular outline button,
`absolute bottom-4 left-[50%] translate-x-[-50%] rounded-full`, shown only when
`!isAtBottom`, with an `ArrowDown` icon (`size-4`). Variant `outline`.

### Download button
`ConversationDownload` — top-right circular outline button with download icon,
exports the transcript to `.md`.

### Empty state
`<ConversationEmptyState>` centered with a `MonolithIcon` (brand mark) at
`h-16 w-16`, title + description. For assistant projects: a buddy/egg avatar in
a `w-20 h-20 rounded-2xl` rarity-gradient tile.

---

## 3. Message bubbles (user vs assistant)

Two layers: generic `ai-elements/message.tsx` primitives + production
`chat/MessageItem.tsx` composition.

### 3.1 Generic `Message` primitive (`ai-elements/message.tsx:39`)
```
group flex w-full max-w-[95%] flex-col gap-2
```
- **User:** `is-user ml-auto justify-end` (right-aligned)
- **Assistant:** `is-assistant` (left-aligned, full width)

### 3.2 `MessageContent` (`ai-elements/message.tsx:52`) — the key style block
```
flex w-fit min-w-0 max-w-full flex-col gap-2 overflow-hidden break-words text-sm
```
Role-conditional:

| Role | Styling |
|------|---------|
| **User bubble** | `group-[.is-user]:ml-auto group-[.is-user]:rounded-2xl group-[.is-user]:bg-muted group-[.is-user]:px-4 group-[.is-user]:py-3 group-[.is-user]:text-foreground` — soft muted-grey bg, **24px radius**, 16/12px padding |
| **Assistant bubble** | `group-[.is-assistant]:w-full group-[.is-assistant]:text-foreground` — **NO bg, NO border, full width** |

> The user bubble is the "Luma-light" style — a comment confirms it was
> previously a dark inverted bubble, now dropped to soft muted grey.

### 3.3 User/assistant distinction summary

| Aspect | User | Assistant |
|--------|------|-----------|
| Alignment | right (`ml-auto`) | left, full width |
| Background | `bg-muted` (#F2F0EE) | none (transparent) |
| Border | none | none |
| Border radius | 24px (`rounded-2xl`) | none |
| Max width | 95% | 100% |
| Content | plain text (whitespace-pre-wrap) | markdown (streamdown) |
| Long content | collapses >300px | flows naturally |
| Above content | file attachments | ToolActionsGroup (collapsed) |
| Below content | — | MediaPreview, DiffSummary |
| Footer (hover) | copy button | timestamp + token usage + copy |

---

## 4. User message (`MessageItem.tsx`)

- **File attachments** (if any) via `FileAttachmentDisplay`, then text inside a
  `motion.div` with `whitespace-pre-wrap break-words`.
- **Long-message collapse** at `COLLAPSE_HEIGHT = 300`px:
  - `motion.div animate={{height}}` tweens between 300px and auto
    (`transition={{duration:0.28, ease:[0.32,0.72,0,1]}}`, `initial={false}`).
  - A `bg-gradient-to-t from-muted to-transparent h-16` fade overlay at bottom.
  - Ghost `size="sm"` button toggles 展开/收起 (CaretDown / CaretUp, 12px).
- System notices starting with `[__IMAGE_GEN_NOTICE__` are hidden entirely.
- **Footer** (right-aligned for user): timestamp
  `text-xs text-muted-foreground/50`, optional `CopyButton`.

---

## 5. Assistant message (`MessageItem.tsx`)

Structure (top to bottom):

1. **`ToolActionsGroup`** — collapsed tool calls + thinking (see §7, §8).
2. **`MediaPreview`** — images/video/audio from tool results (see §11). Rendered
   *outside* the tool group so images stay visible.
3. **`AssistantContent`** (`MessageItem.tsx:964`, memoized) — parses text for
   special fenced blocks in priority order:
   - `show-widget` → Generative UI
   - `batch-plan` → batch plan card
   - `image-gen-result` → image result
   - `image-gen-request` → image request
   - otherwise → `DevOutputSegment` (clickable chips for file refs /
     localhost URLs) or plain `<MessageResponse>` (markdown)
4. **`DiffSummary`** — file-change cards (see §10).
5. **Footer** (`MessageItem.tsx:847`):
   `flex items-center gap-2 opacity-0 group-hover:opacity-100 transition-opacity duration-200`
   (actions only on hover), containing:
   - assistant timestamp (`text-xs text-muted-foreground/50`)
   - optional `TokenUsageDisplay` (total tokens + cost, hover popover breaks
     down in/out/cache)
   - `CopyButton` (ghost, 12px copy icon → `Check` on success,
     `text-muted-foreground/60 hover:text-muted-foreground`)

---

## 6. Streaming / typing indicator

Source: `src/components/chat/StreamingMessage.tsx`. `StreamingMessage` (`:285`)
appends to the list while `isStreaming`. Renders `<AIMessage>` containing the
live `ToolActionsGroup` + buffered markdown + a status bar.

### `ThinkingPhaseLabel` (`:186`)
Shown only when no content/tools/thinking yet. `<Shimmer>` text escalating over
time:
- 0–5s: "Generating…"
- 5–15s: "Responding…"
- 15s+: "Preparing response…"

(Deliberately avoids the word "thinking" to not mislead.)

### `ElapsedTimer` (`:205`)
`tabular-nums` "Xm Ys" / "Ys" counter, guarded against NaN `startedAt`.

### `StreamingStatusBar` (`:247`)
```
flex items-center gap-3 py-2 px-1 text-xs text-muted-foreground
```
- Status text in `<Shimmer duration={1.5}>`.
- Tool running >60s → amber warning "Running longer than usual"
  (`text-status-warning-foreground`).
- >90s → red "Tool may be stuck" + **Force stop** outline button
  (`border-status-error-border bg-status-error-muted`, 10px text).

### Smart buffering (`useBufferedContent`, `:126`)
Holds initial text until ≥40 words or 2.5s, but bypasses immediately for
structured fences (`show-widget`/`batch-plan`/`image-gen-request`) so JSON
renders progressively. Prevents first-character flicker.

### `Shimmer` (`ai-elements/shimmer.tsx`)
`motion.create("p")` text-shimmer: gradient background sweeping left→right
infinitely, `text-transparent` with `bg-clip-text`. `duration` default 2s,
`spread` 2 (scaled by text length).

**WinUI:** shimmer = a `TextBlock` with a `Composition` linear-gradient brush
animated via `LinearGradientBrush` + `Storyboard` on `StartPoint`/`EndPoint`, or
a `CompositionLinearGradientBrush` with `ColorStops` animated. See
[04](04-winui-mapping.md) §"Animation".

---

## 7. Tool-call rendering (CRITICAL — not cards)

> ⚠️ There are TWO tool renderers. The generic card-based `ai-elements/tool.tsx`
> is **NOT** what the chat uses. The chat uses the compact-row
> **`ToolActionsGroup`**.

Source: `src/components/ai-elements/tool-actions-group.tsx`.

### 7.1 `ToolActionsGroup` (`:526`)
Groups ALL tool calls + thinking in one collapsible block, capped at
`w-[min(100%,48rem)]` (max 768px).

**Collapsed header** (`:591`):
```
flex w-full items-center gap-2 px-2 py-1 text-xs rounded-md hover:bg-muted/30
```
- Count badge: `inline-flex items-center justify-center rounded bg-muted/80 px-1.5 py-0.5 text-[10px] font-medium leading-none text-muted-foreground/70 tabular-nums` (total tools + thinking).
- Summary text (`text-muted-foreground/60`): "2 running · 1 completed" / "generating response" / "N actions".
- Running task description in mono, shimmer-wrapped while running.
- `CaretRight` (12px) at `ml-auto`, rotates 90° when expanded.

**Expanded body** (`:621-658`): framer-motion height tween (0.15s), then
fade/slide (0.12s). Content sits in:
```
ml-1.5 mt-0.5 border-l-2 border-border/50 pl-2
```
**A left vertical line like a blockquote — NOT cards.** Auto-expands while a
tool is running or streaming; remembers user toggle.

### 7.2 `ToolActionRow` (`:449`) — one row per tool
```
flex items-center gap-2 px-2 py-1 min-h-[28px] text-xs hover:bg-muted/30 rounded-sm
```
| Element | Styling |
|---------|---------|
| Tool icon | `CodePilotIcon` mapped by `TOOL_REGISTRY`: bash→`terminal`, write/edit→`edit`, read→`file`, search/glob/grep→`search`, agent→`assistant`, fallback→`wrench`. All `size="sm" text-muted-foreground`. |
| Optional label | (e.g. "Edit", "Read") `font-medium text-muted-foreground` |
| Summary | `font-mono text-muted-foreground/60 truncate flex-1` (bash command, filename, search pattern) |
| File path | `text-muted-foreground/40 text-[11px] font-mono truncate max-w-[200px] hidden sm:inline` |
| Image icon | if tool returned media |
| StatusDot | see §7.4 |

### 7.3 Inline tool detail (`renderDetail`)
Only for **bash** and **agent** tools, shown beneath the row while running/done:

**Bash** (`:115`):
```
mt-1 rounded-lg bg-muted/40 px-2.5 py-2 font-mono text-[11px] text-muted-foreground/80 max-h-[140px] overflow-auto whitespace-pre-wrap break-all
```
Shows `$ <command>`, then last 5 lines while running, or up to 20 lines +
"… +N lines" when done.

**Agent** (`:166`): `ml-4 border-l-2 border-border/30 pl-2` with per-line status
icons (`>` active=spinner, `[+]` done=green check, `[x]` error=red x), last 8 lines.

### 7.4 `StatusDot` (`:242`)
Animated via `AnimatePresence mode="wait"`:

| State | Visual |
|-------|--------|
| running | `<SpinnerGap size=14 animate-spin text-muted-foreground/50>` |
| success | `<CheckCircle size=14 text-green-500>` (spring scale-in) |
| error | `<XCircle size=14 text-red-500>` |

### 7.5 Context grouping (`:329`)
3+ consecutive read/search/grep/ls tools auto-collapse into a `ContextGroup`:
- Label: "Gathering context (N)" / "Gathered context (N files)"
- Own expand + search icon + group StatusDot.

### 7.6 ASCII sketch

```
▼ [3] 2 running · 1 completed                          ▾
│   🛠 Edit   src/foo.ts                          [✓]
│   ⌛ Bash   npm run build                        ⟳
│       $ npm run build
│       > building...
│       > done in 4.2s
│   🔍 Read    src/bar.ts                         [✓]
▲
```

**WinUI:** This is the biggest missing piece. Build as a `UserControl` with:
- A header `Button` (toggle) with a count `Border` + summary `TextBlock` + chevron.
- An `ItemsControl`/`ItemsRepeater` for rows, each row a `Grid` (icon col +
  label col + summary col + status col) with `BorderThickness="2,0,0,0"` on the
  container for the vertical line.
- `ProgressRing` for the spinner status; `FontIcon` for check/x.
- `Expander`-style height animation via `VisualStateManager` + `Storyboard`.

---

## 8. Thinking / reasoning blocks

Three distinct mechanisms exist. The chat uses **mechanism A** (`ThinkingRow`).

### A. Inline streaming thinking — `ThinkingRow` (`tool-actions-group.tsx:382-443`)
A compact row **styled identically to a tool-action row** (lives inside
`ToolActionsGroup`):
- Button row: `flex items-center gap-2 px-2 py-1 min-h-[28px] text-xs hover:bg-muted/30 rounded-sm`, full width.
- **Icon swap on hover:** normally `<CodePilotIcon name="assistant">` (Robot01Icon); on hover swaps to `CaretRight` (14px) that rotates 90° when expanded.
- **Summary text:** `font-mono text-muted-foreground/60 truncate flex-1`, derived from first `**bold**` or `# heading` in the content; during streaming wrapped in `<Shimmer duration={1.5}>`. Defaults to "Thinking…" / "Thought".
- **Expanded content:** `ml-6 px-2 py-1.5 text-xs text-muted-foreground/70 border-l-2 border-border/30 prose prose-sm dark:prose-invert max-w-none` containing `<Streamdown>`. Animated with framer-motion height tween (0.15s easeOut).
- **Default open during streaming, collapsed in history.** Expanding calls `stopScroll()` to detach from auto-scroll.

### B. Generic reasoning primitive (`ai-elements/reasoning.tsx`)
Full `Collapsible` (Radix) with `ReasoningContext`. Auto-opens when streaming
starts, auto-closes 1s after streaming ends (once only), unless
`defaultOpen={false}`. `ReasoningTrigger` (`:169`):
`flex w-full items-center gap-2 text-muted-foreground text-sm hover:text-foreground`,
assistant icon + message ("Thinking…" shimmer while streaming, "Thought for N
seconds" after) + `CaretDown` (rotates 180°). `ReasoningContent` (`:211`):
`mt-4 text-sm text-muted-foreground`, slide animations, `<Streamdown>`.

### C. Chain-of-thought primitive (`ai-elements/chain-of-thought.tsx`)
Timeline-style stepper (less used in main chat). Vertical connecting line
(`absolute top-7 bottom-0 left-1/2 -mx-px w-px bg-border`), status colors
(active=foreground, complete/pending=muted), `Badge variant="secondary"` chips.

**WinUI:** implement mechanism A (the production one). A `ToggleButton` row with
`PointerEntered`/`PointerExited` swapping the icon, a mono `TextBlock` summary,
and a `ContentControl`/`Border` for the expanded body with height animation.

---

## 9. Code blocks

Source: `src/components/ai-elements/code-block.tsx` (1089 lines).

### 9.1 Highlighting
**Shiki** with dual light/dark themes resolved from the active theme family
(`resolveShikiThemes`). Two bounded LRU caches: highlighter (10 entries, keyed
`lang:light:dark`) and tokens (200 entries). Unknown languages fall back to
`text`. Async highlighting: renders raw tokens synchronously, then swaps in
highlighted tokens via subscriber callback.

### 9.2 `CodeBlockContainer` (`:457`)
```
group relative w-full overflow-hidden rounded-md border bg-background text-foreground
```
With `data-language`, `contentVisibility:"auto"`, `containIntrinsicSize:"auto 200px"`.

### 9.3 Default header (`CodeBlockDefaultHeader`, `:723`)
```
flex items-center justify-between px-4 py-1.5 text-xs border-b
```
- **Left:** language icon (`getLanguageIcon`: Terminal for bash/sh, Code for
  ts/js/json, Hash for py/go/rust, File for css/html, FileCode fallback) at 14px
  + optional filename (`truncate font-medium`) + `|` separator + **language badge**
  (`rounded px-1.5 py-0.5 bg-accent text-accent-foreground`, uppercase).
- **Right:** three icon-text buttons — **Preview** (only for
  html/xml/jsx/tsx/json/diff/csv/markdown etc., opens artifact panel), **Copy**
  (`CodePilotIcon copy` → `Check` "Copied"), **Markdown**
  (`CodePilotIcon file_code` → "Copy as Markdown" fence).

### 9.4 Terminal styling
For bash/sh/etc., header + body use the `--terminal-*` CSS variables (see
[01](01-design-tokens.md) §1.5). Body: `!bg-[var(--terminal-bg)] !text-[var(--terminal-foreground)]`
— **always dark**, even in light mode.

### 9.5 Body (`CodeBlockBody`, `:395`)
```
<pre className="m-0 p-4 text-sm">
```
Inline `backgroundColor`/`color` from Shiki,
`dark:!bg-[var(--shiki-dark-bg)] dark:!text-[var(--shiki-dark)]`. Optional line
numbers via CSS counters:
`before:content-[counter(line)] before:w-8 before:mr-4 before:text-right before:text-muted-foreground/50`.

### 9.6 Collapse (`:534`)
If `collapsible` and `> COLLAPSE_THRESHOLD (20)` lines: show first
`VISIBLE_LINES (10)` with `h-16 bg-gradient-to-t from-muted` fade + full-width
toggle button ("Expand all N lines" / "Collapse", CaretDown/Up), animated via
`max-height` tween (300ms).

### 9.7 Markdown code in chat (`chat/markdown-components.tsx:139`)
Fenced code inside assistant messages overrides `pre` to a **Widget-card** frame:
- Outer: `my-4 rounded-xl bg-muted/20 overflow-hidden`.
- Header bar: `flex items-center justify-between gap-2 bg-muted/30 px-3 py-1` —
  left: `text-[10px] font-mono uppercase tracking-wider text-muted-foreground/70`
  language label; right: single copy button (`h-7 w-7 px-0`).
- Body `<pre>`: `overflow-x-auto px-4 py-3 text-sm font-mono leading-relaxed`.

**Inline code** (`ChatInlineCode`):
`rounded bg-muted px-1.5 py-0.5 font-mono text-[0.875em] text-foreground`.

**WinUI:** code blocks need a syntax highlighter. Options: (a) port a C++ Shiki
equivalent (heavy), (b) use `TextBlock` with `Run`s colored by a lightweight
tokenizer per language, (c) embed a WebView2 rendering Shiki (full fidelity but
heavy). The terminal-mode (bash) block is straightforward — dark `Border` with
monospace `TextBlock`. See [04](04-winui-mapping.md) §"Code blocks".

---

## 10. Other markdown elements (`chat/markdown-components.tsx`)

Every element is overridden to match the Widget-card design language:

| Element | Class string |
|---------|-------------|
| **Tables** (`ChatTable`) | `my-4 rounded-xl bg-muted/20 overflow-hidden`; header bar `bg-muted/30 px-2 py-1` with right-aligned Copy-Markdown + Export-PNG buttons; body `overflow-x-auto px-3 pb-3 pt-2`, `w-full text-sm border-collapse` |
| `thead` | `border-b border-border/60 bg-muted/40`; `th` `px-3 py-2 text-left font-medium` |
| `tbody` rows | `border-b border-border/30` (last-child no border); `td` `px-3 py-2 align-top` |
| **h1** | `mt-6 mb-3 text-2xl font-semibold tracking-tight` |
| **h2** | `text-xl` (font-semibold, same spacing) |
| **h3** | `text-lg` |
| **h4** | `text-base` |
| **Paragraphs** | `my-3 leading-7` |
| **ul/ol** | `my-3 ml-5 list-disc/list-decimal space-y-1.5 marker:text-muted-foreground/60` |
| **li** | `pl-1.5 leading-7` |
| **blockquote** | `my-4 border-l-4 border-border pl-4 py-1 text-muted-foreground italic` |
| **hr** | `my-6 border-border/50` |
| **link** | `text-primary underline underline-offset-4 decoration-primary/30 hover:decoration-primary` (external → new tab) |
| **strong** | `font-semibold text-foreground` |
| **img** | `my-3 max-w-full rounded-lg border border-border/40` (lazy) |
| **inline code** | `rounded bg-muted px-1.5 py-0.5 font-mono text-[0.875em] text-foreground` |

**WinUI:** map these to `RichTextBlock` `Paragraph`/`Run` properties, or build a
markdown→XAML converter producing native elements. Tables need a `Grid` or
`Border`+`Grid` per row.

---

## 11. File-change cards (`DiffSummary`)

Source: `src/components/chat/DiffSummary.tsx`. Rendered below assistant messages
that wrote/edited files. Two tiers:

### Previewable files (.md/.mdx/.html/.htm/.jsx/.tsx/.csv/.tsv) → `ArtifactFileCard`
```
mt-2 flex items-center gap-3 rounded-lg border border-border/50 bg-card px-4 py-3
```
- **Left:** filename (`text-sm font-medium`) with edit icon + inline **operation pill**:
  - Created = `bg-emerald-500/10 text-emerald-600` (#1A10B981 / #059669)
  - Modified = `bg-amber-500/10 text-amber-600` (#1AF59E0B / #D97706)
  - Pill: 10px text.
- **Below:** absolute path `font-mono text-[10px] text-muted-foreground/60 truncate`.
- **Right:** outline "Open preview" button (preview icon) + optional ghost long-screenshot export.

### Other files → collapsed trailing line
```
mt-2 flex items-center gap-1.5 text-[11px] text-muted-foreground/60
```
"Also modified: a.ts, b.json".

**WinUI:** a `Border` (CornerRadius 8) with a `Grid`: filename col + pill
(small `Border` with colored bg) + path row + preview button. Bind to a
`FileChangeViewModel` collection.

---

## 12. Media preview (`MediaPreview.tsx`)

`mt-2 space-y-2`.

| Type | Styling |
|------|---------|
| Images | `flex flex-wrap gap-2`, each `<img>` `max-w-xs max-h-64 rounded-md border border-border/50 cursor-pointer hover:opacity-90 object-contain`, lazy, opens `ImageLightbox` |
| Videos | `max-w-md max-h-80 rounded-md border`, native controls |
| Audio | `w-full max-w-md` |

**WinUI:** `Image` (or `PersonPicture`/`Border`+`Image`) with `MaxWidth`/`MaxHeight`,
`CornerRadius=6`, click → `ContentDialog` lightbox. Videos: `MediaPlayerElement`.

---

## 13. The composer (input box)

> Detailed component spec in [03](03-components.md) §"Composer". Summary here for
> context.

Composed of `MessageInput` (logic) wrapping the generic `PromptInput` primitives.

### Outer shell (`MessageInput.tsx:1016`)
```
bg-[var(--platform-surface-bar)] backdrop-blur-lg px-4 pt-2 pb-1
```
Inner: `mx-auto w-full max-w-3xl`, then `relative` container holding
`SlashCommandPopover`, `CliToolsPopover`, `QuickActions`, and the `PromptInput`.

### The box (`InputGroup`, `ui/input-group.tsx`)
```
border-input dark:bg-input/30 relative flex w-full items-center rounded-2xl border shadow-sm transition-[color,box-shadow]
```
**24px-radius bordered rounded box with soft shadow.** Focus = neutral border
(no colored ring): `has-[[data-slot=input-group-control]:focus-visible]:border-border`.
Chat adds `[&_[data-slot=input-group]]:shadow-[var(--shadow-diffuse)]`.

### Textarea (`PromptInputTextarea` → `InputGroupTextArea`)
`flex-1 resize-none rounded-none border-0 bg-transparent py-3 shadow-none focus-visible:ring-0`;
in chat `min-h-12 px-4 py-3`. Enter submits, Shift+Enter newline, Backspace on
empty removes last attachment, paste of files adds them as attachments.

### Footer (`PromptInputFooter`)
`justify-between gap-1`, split into **Tools** (left) and **Submit** (right).

### Submit button (`FileAwareSubmitButton`)
Circular (`rounded-full`). Icon by status: ready = `ArrowUp` (16px);
streaming/submitted = `Square`/`Stop` (16px); error = `X`. While generating →
stop button (`type="button"`, calls `onStop`). Disabled unless text/badge/files.

---

## 14. Permission / approval prompt

Source: `src/components/chat/PermissionPrompt.tsx` + `ai-elements/confirmation.tsx`.
Built on generic `Confirmation` (an `Alert`): `flex flex-col gap-2`. Shows when a
tool requests approval (`state === "approval-requested"`).

| Part | Styling |
|------|---------|
| `ConfirmationTitle` | an `AlertDescription` |
| `ConfirmationRequest` | tool input, truncated to `MAX_INPUT_LINES=8` / `MAX_INPUT_CHARS=500` |
| `ConfirmationActions` | `flex items-center justify-end gap-2 self-end`, `ConfirmationAction` buttons (`h-8 px-3 text-sm`) |

Special handling: `AskUserQuestion` tools render an interactive question/options
form inside a `Dialog`.

**WinUI:** `Apps/DesktopWinUI/Controls/PermissionDialog.xaml` exists — verify it
matches this spec (Alert-style, not a heavy modal).

---

## 15. Empty / new-chat state

Source: `src/components/chat/NewChatWelcome.tsx`.

```
<div className="flex items-center justify-center gap-3 mb-8">
  <MonolithIcon className="h-9 w-9 shrink-0" />
  <h1 className="text-3xl font-medium tracking-tight text-foreground leading-none">
    {greeting}
  </h1>
</div>
```

The greeting is **time-aware** (`greetKeyForHour`):
- 5–12h: morning greeting
- 12–18h: afternoon
- 18–23h: evening
- 23–5h: night

Question pool depends on context (assistant workspace / named project / general),
with the project name interpolated. SSR/hydration: first paint shows a fixed
neutral question; client effect composes the real greeting.

**WinUI:** `ChatPage.xaml`'s `EmptyStatePanel` already has a centered "C" avatar
+ welcome text (🟡). Upgrade to: 36×36 brand icon + time-aware greeting at
`text-3xl` (30px), `font-medium`, `tracking-tight`.

---

## 16. Below-composer action bar (`ChatComposerActionBar`)

A simple left/right split row (props `left` and `right`).

| Side | Contents |
|------|----------|
| Left | `ModeIndicator`, `RuntimeSelector`, `ChatPermissionSelector` |
| Right | `RunCockpit` — a popover showing context usage, cost, model usage (`RunCockpitPopoverContent` + `ContextUsageIndicator` with `ContextDotMatrix`) |

**WinUI:** a `Grid` with two columns (left `*`, right `Auto`) below the composer.
The current `ChatPage.xaml` has a `StatusText`/`UsageText` row (🟡) — extend to
hold mode/runtime/permission selectors.

---

## WinUI mapping summary for chat content

| CodePilot element | WinUI approach | Current state |
|------------------|----------------|---------------|
| Transcript container | `ScrollViewer` + centered `StackPanel`/`ItemsRepeater` (MaxWidth 768) | 🟡 `ChatPage.xaml` has `ScrollViewer`+`StackPanel` |
| Auto-follow scroll | subscribe to items-changed → `ScrollToBottom`; track "is at bottom" | ❌ |
| User bubble | `Border` CornerRadius=24, bg=muted, right-aligned, MaxWidth=95% | 🟡 simple text only |
| User collapse >300px | `Grid` with `MaxHeight` animation + fade overlay + toggle button | ❌ |
| Assistant markdown | markdown→XAML converter → `RichTextBlock` or native elements | ❌ |
| ToolActionsGroup | `UserControl` + `ItemsRepeater` + vertical-line `Border` + `ProgressRing`/`FontIcon` | ❌ `ToolCallView.xaml` empty |
| Thinking row | `ToggleButton` row + icon swap + mono summary + expandable body | ❌ |
| Code block | `Border` (terminal: dark bg) + `TextBlock`/highlighter + header bar | ❌ |
| Markdown table | `Grid` per row + header styling | ❌ |
| DiffSummary | `Border` + `Grid` + colored pill + preview button | ❌ |
| Media preview | `Image`/`MediaPlayerElement` + lightbox dialog | ❌ |
| Streaming shimmer | `Composition` linear-gradient brush animation | ❌ |
| Empty state | centered brand icon + time-aware greeting | 🟡 basic |
| Permission prompt | `ContentDialog` or inline `Border` (Alert-style) | 🟡 `PermissionDialog.xaml` exists |

See [04](04-winui-mapping.md) for the full translation tables and prioritized
build checklist.
