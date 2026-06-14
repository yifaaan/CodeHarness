# Context and Compaction

Conversation memory and context compaction.

## One-Liner

ContextMemory manages conversation history and token tracking. FullCompaction automatically summarizes old context when approaching token limits.

## ContextMemory

```typescript
class ContextMemory {
  appendUserMessage(input: ContentPart[], origin: PromptOrigin): void;
  appendSystemReminder(content: string): void;
  appendLoopEvent(event: LoopRecordedEvent): void;
  applyCompaction(result: CompactionResult): void;
  clear(): void;

  get messages(): Message[];           // For LLM
  get tokenCount(): number;
  get length(): number;
}
```

## ContextMessage

```typescript
interface ContextMessage {
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: ContentPart[];
  toolCalls?: ToolCall[];
  toolCallId?: string;
  
  // Metadata (internal)
  origin: PromptOrigin;
  tokenCount: number;
  id: string;
  createdAt: Date;
}
```

## Prompt Origins

```typescript
type PromptOrigin = 
  | 'user'              // Direct user input
  | 'system_trigger'    // System-generated
  | 'hook_result'       // Hook output
  | 'background_task'   // Background completion
  | 'compaction_summary' // Compacted history
  | 'skill_activation'  // Skill invoked
  | 'subagent_spawn'    // Subagent task
  | 'subagent_result';  // Subagent result
```

## Pending Tool Results

```typescript
private pendingToolResultIds: Set<string> = new Set();

// Messages with pending results are "open"
// They are not yet included in the message list for LLM
```

## InjectionManager

Injects dynamic content before each step:

```typescript
class InjectionManager {
  inject(): void;           // Called before each step
  onContextClear(): void;
  onContextCompacted(): void;
}
```

### Built-in Injectors

- **PlanModeInjector**: "You are in Plan mode. Research before implementing."
- **PermissionModeInjector**: "Current permission mode: manual. You need approval..."

## PlanMode

```typescript
class PlanMode {
  async enter(): Promise<void>;   // Create plan file
  cancel(): void;                  // Discard plan
  exit(): void;                    // Keep plan file
  clear(): void;                   // Clear plan content
  get isActive(): boolean;
}
```

Plan files: `<homedir>/plans/<planId>.md`

## FullCompaction

```typescript
class FullCompaction {
  async beforeStep(signal: AbortSignal): Promise<void>;
  async begin(data?: { instruction?: string }): Promise<void>;
  cancel(): void;
  complete(result: CompactionResult): void;
}

interface CompactionResult {
  summary: string;
  tokenCount: number;
  messageCount: number;
  removedMessages: ContextMessage[];
  retainedMessages: ContextMessage[];
}
```

## Compaction Flow

```
beforeStep():
    1. Check tokenCount vs threshold (75% of max)
    2. If shouldCompact:
       - Emit compaction.started
       - Build compaction prompt
       - Call LLM to generate summary
       - Determine which messages to compact
       - Apply compaction
       - Emit compaction.completed
```

## Compaction Strategy

```typescript
interface CompactionStrategy {
  shouldCompact(usedSize: number, maxSize: number): boolean;
  shouldBlock(usedSize: number, maxSize: number): boolean;
  computeCompactCount(messages: Message[], maxSize: number): number;
}

// Default: trigger at 75%, block at 95%
// Compact all but last 10 messages
```

## Context Lifecycle

```
Agent created
    │
    ├── System prompt set
    │
Conversation loop:
    ├── User prompt → appendUserMessage()
    ├── beforeStep hook → InjectionManager.inject()
    ├── beforeStep hook → FullCompaction.beforeStep()
    ├── LLM generates response
    ├── appendLoopEvent(tool calls)
    ├── Tool execution → appendLoopEvent(tool results)
    └── Token tracking updated
    │
Context clear (/new):
    ├── clear()
    ├── InjectionManager.onContextClear()
    └── System prompt re-initialized
```

## See Also

- [agent-lifecycle.md](agent-lifecycle.md) — TurnFlow integration
- [loop-engine.md](loop-engine.md) — beforeStep/afterStep hooks
