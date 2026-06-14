# CLI and TUI Layer

## Core Purpose

User-facing application handling CLI parsing, interactive TUI, and non-interactive mode.

## CLI Entry

```typescript
// Command-line options
interface CLIOptions {
  session?: string;      // -S, --session [id]
  continue?: boolean;    // -C, --continue
  yolo?: boolean;        // -y, --yolo
  model?: string;        // -m, --model
  prompt?: string;       // -p, --prompt
  plan?: boolean;        // --plan
  outputFormat?: string; // --output-format: "text" | "stream-json"
}
```

## Startup Flow

```
main() → parse CLI args
  │
  ├── validateOptions()
  │
  ├── if non-interactive (--prompt):
  │     runPrompt() → stream to stdout
  │
  └── if interactive:
        runShell() → TUI event loop
```

## TUI Layout

```
┌─────────────────────────────────────────────────────┐
│  transcript (conversation history)                  │
├─────────────────────────────────────────────────────┤
│  activity (current action)                          │
├─────────────────────────────────────────────────────┤
│  todoPanel (task list)                              │
├─────────────────────────────────────────────────────┤
│  queue (pending actions)                            │
├─────────────────────────────────────────────────────┤
│  editor (input area)                                │
├─────────────────────────────────────────────────────┤
│  footer (status bar)                                │
└─────────────────────────────────────────────────────┘
```

## Key Components

| Component | Purpose |
|-----------|---------|
| `AssistantMessage` | Render assistant responses |
| `ToolCall` | Display tool invocations |
| `Thinking` | Show thinking process |
| `ApprovalPanel` | Handle permission requests |
| `QuestionDialog` | Display questions to user |
| `TasksBrowser` | Manage background tasks |

## Reverse-RPC

TUI receives agent events via reverse-RPC:

```
Agent ──events──> SDK ──RPC──> TUI
TUI ──approval──> SDK ──RPC──> Agent
```

## Slash Commands

| Command | Description |
|---------|-------------|
| `/help` | Show help |
| `/new` | New session |
| `/model` | Switch model |
| `/permission` | Change permission mode |
| `/compact` | Compress context |
| `/fork` | Fork session |
| `/login` | OAuth login |

## Theme System

- Dark/light mode detection
- Custom theme files
- Terminal color support

## File Structure

```
apps/kimi-code/src/
├── cli/                    # CLI entry, options, run-prompt, run-shell
├── tui/                    # TUI components, rendering, commands
│   ├── kimi-tui.ts        # Main TUI class
│   ├── components/        # UI components
│   └── commands/          # Slash commands
└── main.ts                 # Entry point
```

---

**See also**: [15-cli-and-tui-layer.md](../15-cli-and-tui-layer.md) for full details.
