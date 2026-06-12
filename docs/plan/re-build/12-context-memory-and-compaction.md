# Context Memory and Compaction

**Source**: `packages/agent-core/src/agent/context/`, `packages/agent-core/src/agent/compaction/`, `packages/agent-core/src/agent/injection/`, `packages/agent-core/src/agent/plan/`

## Purpose

**ContextMemory** manages the conversation history — the list of messages that forms the LLM's context window. It tracks tokens, handles tool call/result pairing, and provides the message list that the loop sends to the LLM.

**Compaction** is the mechanism for reducing context size when it approaches the model's token limit. It summarizes older messages and replaces them with a condensed version.

**Injection** dynamically adds system reminders before each step (plan mode status, permission mode status, etc.).

**PlanMode** manages the "plan mode" state where the agent researches before implementing.

## ContextMemory

**Source**: `packages/agent-core/src/agent/context/index.ts`

```typescript
class ContextMemory {
  constructor(agent: Agent, config: ContextConfig);

  // --- Message management ---
  appendUserMessage(input: ContentPart[], origin: PromptOrigin): void;
  appendSystemReminder(content: string): void;
  appendMessage(message: ContextMessage): void;
  clear(): void;

  // --- Tool call/result handling ---
  appendLoopEvent(event: LoopRecordedEvent): void;
  markLastUserPromptBlocked(): void;

  // --- Compaction ---
  applyCompaction(result: CompactionResult): void;

  // --- State queries ---
  get messages(): Message[];           // Current message list for LLM
  get data(): ContextMessage[];        // Full internal state (with metadata)
  get tokenCount(): number;            // Current token estimate
  get tokenCountWithPending(): number; // Including pending tool results
  get length(): number;                // Number of messages
  get openSteps(): number;             // Steps with pending tool results

  // --- Token counting ---
  estimateTokenCount(text: string): number;
}

interface ContextConfig {
  maxContextSize: number;          // Max tokens before compaction
  compactionThreshold: number;     // % of maxContextSize to trigger compaction
  systemPrompt: string;            // Base system prompt
}
```

### ContextMessage

```typescript
interface ContextMessage {
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: ContentPart[];
  toolCalls?: ToolCall[];
  toolCallId?: string;
  
  // Extended metadata (internal, not sent to LLM)
  origin: PromptOrigin;
  tokenCount: number;
  id: string;                     // Unique message ID
  createdAt: Date;
}
```

### PromptOrigin

```typescript
type PromptOrigin = 
  | 'user'                       // Direct user input
  | 'system_trigger'             // System-generated (plan activation, etc.)
  | 'hook_result'                // Output from a hook
  | 'background_task'            // Background task completion notification
  | 'compaction_summary'         // Compacted history summary
  | 'skill_activation'           // Skill was activated
  | 'injection'                  // Dynamic injection
  | 'subagent_spawn'             // Subagent task description
  | 'subagent_result'            // Subagent result summary;
```

### Message Lifecycle

```
User input → appendUserMessage()
  → ContextMessage { role: 'user', origin: 'user', ... }
  → Stored in _history array
  → tokenCount updated

Loop step:
  → LLM response with content + toolCalls
  → appendLoopEvent({ type: 'tool.call', ... })
  → appendLoopEvent({ type: 'tool.result', ... })
  → Tool calls/results paired via pendingToolResultIds
  
Compaction:
  → applyCompaction(result)
  → Old messages replaced with summary message
  → tokenCount recalculated
  
Clear:
  → _history = []
  → tokenCount = 0
  → All injection/plan state reset
```

### Pending Tool Results Tracking

```typescript
// Tracks tool calls awaiting results
private pendingToolResultIds: Set<string> = new Set();

appendLoopEvent(event):
  if event.type === 'tool.call':
    pendingToolResultIds.add(event.toolCallId)
    _history.push(contextMessage)
    
  if event.type === 'tool.result':
    pendingToolResultIds.delete(event.toolCallId)
    _history.push(contextMessage)
    
  // Messages with pending results are "open"
  // They are not yet included in the message list for LLM
```

### Message Building for LLM

The `messages` getter projects internal format to kosong `Message[]`:

```
1. Start with system message (from config)
2. Filter out open steps (pending tool results)
3. Convert ContextMessage[] → kosong Message[]
4. Apply message limits (max tokens)
5. Return Message[]
```

## InjectionManager

**Source**: `packages/agent-core/src/agent/injection/manager.ts`

```typescript
class InjectionManager {
  constructor(agent: Agent);

  /** Called before each step — injects dynamic content */
  inject(): void;

  /** Called on context clear */
  onContextClear(): void;

  /** Called after compaction */
  onContextCompacted(): void;
}

interface DynamicInjector {
  inject(context: ContextMemory): void;
  onContextClear(): void;
  onContextCompacted(): void;
}
```

### Built-in Injectors

**PlanModeInjector**: Injects a system reminder about plan mode status:
```
[System reminder: You are in Plan mode. Research before implementing.
 Current plan file has N sections. Use EnterPlanMode to plan, ExitPlanMode to implement.]
```

**PermissionModeInjector**: Injects permission mode status:
```
[System reminder: Current permission mode: manual. 
 You need approval for destructive operations.]
```

Injectors fire before each step via `injection.inject()` called from the `beforeStep` loop hook.

## PlanMode

**Source**: `packages/agent-core/src/agent/plan/index.ts`

```typescript
class PlanMode {
  constructor(agent: Agent);

  /** Enter plan mode — creates plan file */
  async enter(): Promise<void>;

  /** Cancel plan mode (no plan file) */
  cancel(): void;

  /** Exit plan mode (keep plan file) */
  exit(): void;

  /** Clear plan file content */
  clear(): void;

  /** Is plan mode active? */
  get isActive(): boolean;

  /** Get current plan data */
  data(): PlanData | null;
}

interface PlanData {
  planId: string;
  filePath: string;
  content: string;       // Current plan file content
}
```

### Plan Mode Lifecycle

```
User types /plan or Shift-Tab
  → Agent.enterPlan()
    → planMode.enter()
    → Create plan file: <homedir>/plans/<planId>.md
    → Inject system reminder: "You are in Plan mode..."
    → Record: { type: 'plan_mode.enter', planId }

Agent works in plan mode (read-only tools preferred)
  → Plan file may be written to
  → PlanModeInjector adds plan mode reminder each step

User types /plan again or ExitPlanMode tool called
  → Agent.exitPlan() (or cancelPlan)
    → planMode.exit()
    → Remove plan mode reminder
    → Record: { type: 'plan_mode.exit' }

Plan file persists at <homedir>/plans/<planId>.md
```

## FullCompaction

**Source**: `packages/agent-core/src/agent/compaction/full.ts`

```typescript
class FullCompaction {
  constructor(agent: Agent, config: CompactionConfig);

  /** Check if compaction is needed before a step */
  async beforeStep(signal: AbortSignal): Promise<void>;

  /** Post-step cleanup */
  afterStep(): void;

  /** Handle context overflow error from LLM */
  async handleOverflowError(signal: AbortSignal, error: APIContextOverflowError): Promise<void>;

  /** Start manual compaction */
  async begin(data?: { instruction?: string }): Promise<void>;

  /** Cancel in-progress compaction */
  cancel(): void;

  /** Complete compaction with result */
  complete(result: CompactionResult): void;
}

interface CompactionConfig {
  strategy: CompactionStrategy;
  autoCompaction: boolean;         // Auto-trigger compaction
  maxContextSize: number;          // Model's context window
  compactionThreshold: number;     // % to trigger (e.g., 0.75 = 75%)
}

interface CompactionStrategy {
  shouldCompact(usedSize: number, maxSize: number): boolean;
  shouldBlock(usedSize: number, maxSize: number): boolean;
  computeCompactCount(messages: Message[], maxSize: number): number;
}

interface CompactionResult {
  summary: string;                 // LLM-generated summary
  tokenCount: number;              // Token count of summary
  messageCount: number;            // Messages replaced
  removedMessages: ContextMessage[];  // Messages that were compacted
  retainedMessages: ContextMessage[]; // Messages kept (recent ones)
}
```

### Compaction Flow

```
beforeStep():
  1. Check tokenCount vs maxContextSize * compactionThreshold
  2. If shouldCompact:
     → Emit compaction.started event
     → Build compaction prompt:
       "Summarize the following conversation, preserving key decisions, 
        code changes, file paths, and user requirements..."
     → Call LLM (kosong) to generate summary
       (tools disabled — pure text generation)
     → Receive summary text
     → Determine which messages to compact:
       compact all messages except the last N (most recent)
     → Apply compaction:
       context.applyCompaction({
         summary,
         tokenCount: estimate(summary),
         messageCount: compactedCount,
         removedMessages,
         retainedMessages,
       })
     → Emit compaction.completed event
     → Record: { type: 'full_compaction.complete', ... }

handleOverflowError():
  1. If LLM returns APIContextOverflowError:
     → Force compaction immediately
     → Retry the step with compacted context
     → If compaction fails → report error to user
```

### Compaction Prompt

The compaction LLM call uses a specialized system prompt:

```
You are a conversation summarizer. Your task is to condense the 
conversation history while preserving:
1. User requirements and goals
2. Key decisions made
3. File paths and code changes discussed
4. Important findings from research
5. Current task status

Write a concise summary. Be specific — include file paths and 
technical details when mentioned.
```

### Compaction Strategy

```typescript
class DefaultCompactionStrategy implements CompactionStrategy {
  shouldCompact(usedSize: number, maxSize: number): boolean {
    // Trigger at 75% of max context
    return usedSize > maxSize * 0.75;
  }
  
  shouldBlock(usedSize: number, maxSize: number): boolean {
    // Block new turns at 95%
    return usedSize > maxSize * 0.95;
  }
  
  computeCompactCount(messages: Message[], maxSize: number): number {
    // Compact all but the last 10 messages
    return Math.max(0, messages.length - 10);
  }
}
```

## Context Lifecycle Diagram

```
┌────────────────────────────────────────────────────────────────────┐
│  CONTEXT MEMORY LIFECYCLE                                          │
│                                                                     │
│  Agent created                                                     │
│    │                                                                │
│    ├── System prompt set                                           │
│    │                                                                │
│    ▼                                                                │
│  Conversation (repeated):                                          │
│    │                                                                │
│    ├── User prompt → appendUserMessage()                           │
│    ├── beforeStep hook → InjectionManager.inject()                 │
│    │                     └── PlanModeInjector (if in plan mode)    │
│    │                     └── PermissionModeInjector                │
│    ├── beforeStep hook → FullCompaction.beforeStep()               │
│    │   └── If tokenCount > threshold:                              │
│    │       └── Summarize old messages via LLM                      │
│    │       └── applyCompaction() → replace old with summary        │
│    ├── LLM generates response                                      │
│    ├── appendLoopEvent(tool calls)                                 │
│    ├── Tool execution → appendLoopEvent(tool results)              │
│    └── Token tracking updated                                      │
│                                                                     │
│  Context clear (e.g., /new):                                       │
│    ├── clear()                                                     │
│    ├── InjectionManager.onContextClear()                           │
│    └── System prompt re-initialized                                │
│                                                                     │
│  Session end                                                       │
└────────────────────────────────────────────────────────────────────┘
```

## Re-implementation Notes

1. **Context is append-only**: Messages are never removed (except by compaction, which replaces a range with a summary). This makes the data structure simple — a growable array.

2. **Token estimation is approximate**: The `estimateTokenCount()` function uses character count / 4 (a rough approximation). Accurate counting would require tokenizing, which is model-specific and expensive.

3. **Compaction creates a circular dependency**: Compaction calls the LLM to summarize context, but the LLM call itself uses the context that compaction is trying to reduce. This is handled by disabling tools during compaction and using a minimal system prompt.

4. **InjectionManager fires before each step**: The injectors add system reminders that the LLM sees before generating a response. These are ephemeral — they're added before each step and cleared after (they're not persisted in the message history).

5. **Plan mode creates a file**: Plan files are written to `<agentHomedir>/plans/<planId>.md`. The plan file is a plain Markdown file that the agent writes to during planning. The PlanModeInjector reads the file content and injects it as context each step.

6. **Compaction strategy is pluggable**: The `DefaultCompactionStrategy` triggers at 75% and blocks at 95%. Different models with different context windows may need different thresholds. The strategy interface allows customization.