# CLI and TUI Layer

**Source**: `apps/kimi-code/src/`

## Purpose

The CLI/TUI layer is the **user-facing application** — the `kimi` command that users run in their terminal. It handles:

1. **CLI entry**: Command-line argument parsing, validation, and routing
2. **Interactive TUI**: Full terminal UI for conversation, editing, and visualization
3. **Non-interactive mode**: Single-prompt execution (`kimi --prompt "..."`)
4. **Reverse-RPC**: Bridges agent events to TUI rendering

This is the most platform-specific layer. The TUI framework (`@earendil-works/pi-tui`) is a custom terminal UI library that would need a complete rewrite for another platform.

## CLI Entry Point

**Source**: `apps/kimi-code/src/main.ts`

```typescript
function main(): void {
  // 1. Initialize process
  initProcessName('kimi');
  installCrashHandlers();      // Telemetry crash reporting
  installNativeModuleHook();   // Native binary support
  
  // 2. Run smoke test if needed
  runSmokeTest();
  
  // 3. Start background cache cleanup
  startStaleNativeCacheCleanup();
  
  // 4. Parse CLI arguments
  const program = createProgram();
  program.parse(process.argv);
}

function createProgram(): Command {
  // Commander.js program with:
  // --session/-S     Resume specific session
  // --continue/-C    Continue most recent session
  // --yolo/-y        Auto-approve mode
  // --model/-m       Model override
  // --prompt/-p      Non-interactive prompt
  // --plan           Plan mode
  // --output-format  Output format for --prompt
  // --skills-dir     Custom skill directories
  // -V/--version     Version info
  // --export         Export session
}
```

### CLI Options

**Source**: `apps/kimi-code/src/cli/options.ts`

```typescript
interface CLIOptions {
  session?: string;          // -S, --session [id]
  continue?: boolean;        // -C, --continue
  yolo?: boolean;            // -y, --yolo
  plan?: boolean;            // --plan
  model?: string;            // -m, --model
  prompt?: string;           // -p, --prompt
  outputFormat?: string;     // --output-format: "text" | "stream-json"
  skillsDir?: string[];      // --skills-dir (repeatable)
}

interface ValidatedOptions {
  options: CLIOptions;
  uiMode: 'shell' | 'print';  // shell = TUI, print = non-interactive
}
```

### Flag Conflict Rules

```
--continue  AND  --session      →  Reject (both mean "resume")
--yolo      AND  --continue     →  Reject (YOLO cannot resume)
--yolo      AND  --session      →  Reject
--plan      AND  --continue     →  Reject
--plan      AND  --session      →  Reject
--prompt    AND  --yolo         →  Reject (prompt mode = auto-approve)
--prompt    AND  --plan         →  Reject
--output-format WITHOUT --prompt → Reject
```

### Startup Flow

```
main() → createProgram().parse()
  │
  ▼
handleMainCommand(opts, version)
  │
  ├── validateOptions(opts)
  │     └── Returns { options, uiMode: 'shell' | 'print' }
  │
  ├── Update preflight check
  │
  ├── if uiMode === 'print':
  │     runPrompt(opts)          → non-interactive mode
  │
  └── if uiMode === 'shell':
        runShell(opts)           → interactive TUI mode
```

## Non-Interactive Mode (runPrompt)

**Source**: `apps/kimi-code/src/cli/run-prompt.ts`

```
runPrompt(opts):
  1. Create KimiHarness (SDK)
  2. Resolve/setup OAuth and config
  3. Create session
  4. Send prompt to agent
  5. Stream response to stdout:
     - text format: print assistant text directly
     - stream-json format: emit JSON events per line
  6. Close session
```

## Interactive TUI Mode (runShell)

**Source**: `apps/kimi-code/src/cli/run-shell.ts`

```
runShell(opts):
  1. Load TUI config from ~/.kimi-code/tui.toml (theme, editor, etc.)
  2. Detect terminal theme (dark/light mode)
  3. Create KimiHarness
  4. Create KimiTUI(harness, config)
  
  5. Check for legacy ~/.kimi/ migration:
     → If detected, run migration screen
  
  6. Initialize telemetry
  
  7. tui.start():
     → Create or resume session
     → Start pi-tui event loop
     → Subscribe to agent events
     → Render welcome screen
     → Wait for user input
```

## KimiTUI (Terminal UI)

**Source**: `apps/kimi-code/src/tui/kimi-tui.ts`

The `KimiTUI` class is a ~5200-line component that implements the entire terminal user interface. It's built on `@earendil-works/pi-tui`.

### TUI State

```typescript
interface TUIState {
  transcript: TranscriptEntry[];     // All conversation entries
  livePane: LivePaneState | null;    // Current streaming content
  toolCalls: Map<string, ToolCallState>;  // Active tool calls
  pendingApprovals: ApprovalRequest[];
  pendingQuestions: QuestionRequest[];
  
  // UI state
  isPlanMode: boolean;
  isYoloMode: boolean;
  currentModel: string;
  permissionMode: PermissionMode;
  theme: Theme;
  editorConfig: EditorConfig;
  
  // Dialogs
  activeDialog: DialogState | null;
}
```

### Layout (pi-tui based)

```
┌─────────────────────────────────────────────────────┐
│ MoonLoader (thinking indicator)   UsagePanel        │
│                                                     │
│ ┌─────────────────────────────────────────────────┐ │
│ │               Transcript Container              │ │
│ │  ┌────────────────────────────────────────┐     │ │
│ │  │ UserMessageComponent                    │     │ │
│ │  │ "帮我梳理这个项目的架构"                 │     │ │
│ │  └────────────────────────────────────────┘     │ │
│ │  ┌────────────────────────────────────────┐     │ │
│ │  │ AssistantMessageComponent               │     │ │
│ │  │ Let me explore the project structure... │     │ │
│ │  │ ┌────────────────────────────────────┐  │     │ │
│ │  │ │ ToolCallComponent (Grep)           │  │     │ │
│ │  │ │ Searching src/**/*.ts...           │  │     │ │
│ │  │ └────────────────────────────────────┘  │     │ │
│ │  │ ┌────────────────────────────────────┐  │     │ │
│ │  │ │ ToolResultComponent                │  │     │ │
│ │  │ │ Found 42 files                     │  │     │ │
│ │  │ └────────────────────────────────────┘  │     │ │
│ │  └────────────────────────────────────────┘     │ │
│ │  ┌────────────────────────────────────────┐     │ │
│ │  │ ThinkingComponent                      │     │ │
│ │  │ Thinking... ███░░░░░ 60%              │     │ │
│ │  └────────────────────────────────────────┘     │ │
│ └─────────────────────────────────────────────────┘ │
│                                                     │
│ ┌─────────────────────────────────────────────────┐ │
│ │  Input Editor (CustomEditor)                     │ │
│ │  > /help                                         │ │
│ └─────────────────────────────────────────────────┘ │
│                                                     │
│ Footer: Model | Mode | Status | Tasks              │
└─────────────────────────────────────────────────────┘
```

### Components (All 123 Files)

**Chrome**:
- `MoonLoader` — Thinking/streaming animation
- `Footer` — Status bar (model, mode, task count)
- `GutterContainer` — Side gutter for diagnostics
- `TodoPanel` — Task list panel
- `WelcomeScreen` — Startup banner

**Editor**:
- `CustomEditor` — Input area with:
  - `@` file mention autocomplete
  - `/` slash command completion
  - Image/video paste support (Ctrl-V / Alt-V)
  - External editor integration (Ctrl-G)
  - Input history (↑/↓)
  - Undo support (Ctrl--)

**Messages**:
- `UserMessageComponent`
- `AssistantMessageComponent`
- `ToolCallComponent` + `ToolResultComponent`
- `ThinkingComponent` — Animated thinking display
- `PlanBoxComponent` — Plan mode card
- `StatusPanelComponent`
- `UsagePanelComponent` — Token usage display
- `SkillActivationComponent`

**Dialogs**:
- `ApprovalPanel` — Tool call approval dialog
- `QuestionDialog` — AskUserQuestion rendering
- `SessionPicker` — Session list/restore
- `ModelSelector` — Model switching
- `ThemeSelector` — Theme selection
- `SettingsSelector` — Settings panel
- `HelpPanel` — `/help` keyboard shortcuts
- `TasksBrowser` — Background task list
- `TaskOutputViewer` — Task output display
- `CompactionProgress` — Compaction progress

### Theme System

```typescript
interface Theme {
  name: string;
  colors: {
    primary: string;        // Brand color
    secondary: string;
    background: string;
    foreground: string;
    accent: string;
    error: string;
    success: string;
    warning: string;
    
    // Component-specific
    userMessage: string;
    assistantMessage: string;
    toolCall: string;
    toolResult: string;
    thinking: string;
    planCard: string;
    
    // Editor
    editorBackground: string;
    editorForeground: string;
    editorCursor: string;
    editorSelection: string;
  };
}
```

Built-in themes: dark, light. Auto-detection from terminal background color.

## Reverse-RPC

**Source**: `apps/kimi-code/src/tui/reverse-rpc/`

The TUI communicates with the agent through **reverse-RPC** — the agent calls methods on the TUI to request approvals, ask questions, and emit events.

```typescript
// Registered in KimiTUI constructor:
registerReverseRPCHandlers() {
  approvalController = new ApprovalController(this);
  questionController = new QuestionController(this);
  
  // Agent requests user approval for a tool call:
  this.harness.onRequestApproval(async (request) => {
    return approvalController.requestApproval(request);
  });
  
  // Agent asks user a structured question:
  this.harness.onQuestion(async (question) => {
    return questionController.askQuestion(question);
  });
  
  // Agent emits events (streaming, status, etc.):
  this.harness.addEventListener('assistant.delta', (event) => {
    this.handleAssistantDelta(event);
  });
  
  this.harness.addEventListener('tool.call.started', (event) => {
    this.handleToolCallStarted(event);
  });
  
  // ... 20+ event handlers
}
```

### Event → UI Rendering Pipeline

```
AgentEvent
  │
  ├── assistant.delta
  │     → renderAssistantDelta(content)
  │     → Update livePane in TUIState
  │     → Re-render transcript container
  │
  ├── thinking.delta
  │     → renderThinkingDelta(content)
  │     → Update MoonLoader/ThinkingComponent
  │
  ├── tool.call.started
  │     → addToolCall(toolCallId, name, args)
  │     → Render ToolCallComponent
  │
  ├── tool.progress
  │     → updateToolCall(toolCallId, update)
  │     → Stream output to ToolResultComponent
  │
  ├── tool.result
  │     → finalizeToolCall(toolCallId, result)
  │     → Render final ToolResultComponent
  │
  ├── turn.ended
  │     → finalizeTranscript()
  │     → Enable input editor
  │
  └── error
        → renderError(error)
        → Show error in transcript
```

### Modal Coordinator

Dialogs are managed by `ModalCoordinator` to prevent stacking issues:

```typescript
class ModalCoordinator {
  show(dialog: Dialog): Promise<DialogResult>;
  dismiss(dialogId: string): void;
  get topDialog(): Dialog | null;
  dismissAll(): void;
}
```

Only one dialog can be active at a time. New dialogs queue if one is already showing.

## Commands

**Source**: `apps/kimi-code/src/tui/commands/`

Slash commands are parsed and executed by the TUI:

```typescript
// Command parsing
function handleUserInput(input: string): void {
  if (input.startsWith('/')) {
    executeSlashCommand(input);
  } else {
    sendMessage(input);
  }
}

// Command execution
function executeSlashCommand(input: string): void {
  const [name, ...args] = input.slice(1).split(' ');
  
  switch (name) {
    case 'help': case 'h': case '?': showHelp(); break;
    case 'new': case 'clear': startNewSession(); break;
    case 'model': showModelSelector(); break;
    case 'plan': togglePlanMode(args[0]); break;
    case 'yolo': case 'yes': toggleYoloMode(args[0]); break;
    case 'sessions': case 'resume': showSessionPicker(); break;
    case 'settings': case 'config': showSettings(); break;
    case 'permission': showPermissionSelector(); break;
    case 'mcp': showMcpStatus(); break;
    case 'mcp-config': showMcpConfig(); break;
    case 'usage': showUsage(); break;
    case 'status': showStatus(); break;
    case 'tasks': case 'task': showTasks(); break;
    case 'fork': forkSession(); break;
    case 'title': case 'rename': setTitle(args.join(' ')); break;
    case 'compact': compactContext(args.join(' ')); break;
    case 'init': initClaudeMd(); break;
    case 'theme': showThemeSelector(); break;
    case 'editor': configureEditor(); break;
    case 'login': showLogin(); break;
    case 'logout': handleLogout(); break;
    case 'feedback': showFeedback(); break;
    case 'version': showVersion(); break;
    default:
      // Check if it's a skill command
      if (harness.skills.getSkill(name)) {
        harness.activateSkill(name, args.join(' '));
      } else {
        sendMessage(input);  // Treat as regular message
      }
  }
}
```

### Available Commands

| Category | Commands |
|----------|----------|
| Account | `/login`, `/logout`, `/model`, `/settings`, `/permission`, `/editor`, `/theme` |
| Session | `/new`, `/sessions`, `/tasks`, `/fork`, `/title`, `/compact`, `/init` |
| Mode | `/yolo`, `/plan`, `/plan clear` |
| Info | `/help`, `/usage`, `/status`, `/mcp`, `/version`, `/feedback` |
| Controls | (Esc, Ctrl-C, Ctrl-D for cancel/exit) |

## Update System

**Source**: `apps/kimi-code/src/cli/update/`

```typescript
interface UpdateInfo {
  currentVersion: string;
  latestVersion: string;
  releaseDate: Date;
  downloadUrl: string;
  isUrgent: boolean;
}

class UpdateChecker {
  async check(): Promise<UpdateInfo | null>;
  async preflight(): Promise<UpdatePreflightResult>;
}
```

Update flow:
1. CLI startup → background version check against CDN
2. If new version available → show notification in TUI
3. If update accepted → download and install
4. CDN URLs and version cache paths are in constants

## Utilities

**Source**: `apps/kimi-code/src/utils/`

| Utility | Purpose |
|---------|---------|
| `paths.ts` | Resolve data/log/input-history file paths |
| `process/proctitle.ts` | Set process title in `ps` output |
| `git/` | Git operations (ls-files, status) for workspace detection |
| `history/input-history.ts` | Per-directory input history persistence |
| `clipboard/` | Image paste from clipboard (platform-specific) |

## Re-implementation Notes

1. **This is the most platform-specific layer**: The TUI framework `@earendil-works/pi-tui` is a custom library. For a reimplementation, you'd need to either:
   - Find equivalent terminal UI library in your language (e.g., Bubble Tea for Go, Textual for Python)
   - Implement the event → render contract as documented in the events section

2. **The event contract is the bridge**: All communication between the agent engine and the UI goes through typed events. If you get the event types right, you can implement any UI on the other end.

3. **Reverse-RPC is the approval/question channel**: The agent calls back to the TUI to request approvals and ask questions. These are blocking operations — the agent waits for user input before continuing.

4. **Non-interactive mode is simpler**: `runPrompt` is just: create harness → send prompt → stream output → exit. Port this first before tackling the TUI.

5. **Input editor features**: The custom editor includes file mention autocomplete (`@`), slash command autocomplete (`/`), image pasting, external editor integration, input history, and undo. These are nice-to-have for a reimplementation; the core flow just needs text input and submit.

6. **Theme system**: Dark/light auto-detection reads terminal background color. Custom themes are defined in `config.toml` or `tui.toml`. For porting, start with a single theme.