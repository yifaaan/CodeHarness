# Agent Turn Flow

**Source**: `packages/agent-core/src/agent/turn/index.ts`

## Purpose

TurnFlow manages the lifecycle of a single conversation turn — from when the user submits a prompt to when the agent finishes responding. It bridges user-facing operations (`prompt`, `steer`, `cancel`) with the stateless loop (`runTurn`).

A "turn" in kimi-code corresponds to one user input and the agent's complete response (which may involve multiple LLM calls and tool executions). A turn contains multiple "steps" — each step is one LLM call + optional tool execution.

## TurnFlow Interface

```typescript
class TurnFlow {
  constructor(agent: Agent);

  /**
   * Start a new turn with the given input.
   * Rejects if a turn is already active.
   * Returns when the turn completes (all steps done).
   */
  prompt(input: ContentPart[], origin: PromptOrigin): Promise<TurnEndResult>;

  /**
   * Submit input while another turn is active.
   * The input is buffered and flushed at the next beforeStep hook.
   * If no turn is active, behaves like prompt().
   */
  steer(input: ContentPart[], origin: PromptOrigin): Promise<void>;

  /**
   * Cancel the currently active turn via AbortController.
   */
  cancel(turnId?: string): void;

  /**
   * Wait for the current turn to finish.
   */
  waitForCurrentTurn(signal?: AbortSignal): Promise<TurnEndResult>;

  /**
   * Replay operations — these set turn to 'resuming' state without executing.
   */
  restorePrompt(input: ContentPart[], origin: PromptOrigin): void;
  restoreSteer(input: ContentPart[], origin: PromptOrigin): void;
  finishResume(): void;

  // State queries
  get activeTurn(): ActiveTurn | null;
  get turnId(): string | null;
}

interface TurnEndResult {
  event: TurnEndedEvent;
  stopReason: LoopTurnStopReason;
}
```

## Active Turn State Machine

```
       null
        │
   prompt() called
        │
        ▼
  ┌─────────────┐
  │  'resuming' │  (only during replay — no LLM calls)
  └──────┬──────┘
         │ finishResume()
         ▼
  ┌────────────────────┐
  │  Active {          │
  │    controller:     │  AbortController (for cancel)
  │    promise:        │  Promise<TurnEndResult> (completion)
  │    turnId: string  │
  │  }                 │
  └────────┬───────────┘
           │ turn completes (naturally or via cancel)
           ▼
         null
```

## Steer Buffer

The steer buffer allows users to type while the model is generating:

```typescript
interface BufferedSteer {
  input: ContentPart[];
  origin: PromptOrigin;
}

// Internal state:
private _steerBuffer: BufferedSteer[] = [];
```

**Flow**:
```
User types during generation
    │
    ▼
steer("continue but focus on tests")
    │
    ├── If no active turn → behave like prompt()
    └── If active turn → buffer.append({ input, origin })
         │
         ▼
    At next loop step:
    beforeStep hook:
      for each buffered steer:
        context.appendUserMessage(input, origin)
      steerBuffer.clear()
      (model sees "continue but focus on tests" as next input)
```

## Turn Worker (Internal)

The `turnWorker` is the private method that orchestrates turn execution:

```
turnWorker(input, origin):
  1. Record: records.logRecord({ type: 'turn.prompt', input, origin })
  
  2. Create AbortController for this turn
     Set this.activeTurn = { controller, promise, turnId }
  
  3. Apply UserPromptSubmit hook:
     result = hooks.triggerBlock('UserPromptSubmit', { input })
     if result.action === 'block':
       context.markLastUserPromptBlocked()
       emit error event
       end turn
       return
  
  4. Append user message to context:
     context.appendUserMessage(input, origin)
  
  5. Run the loop:
     try {
       result = await runTurn({ ... loop input ... })
     } catch (err) {
       if (isAbortError): emit cancel event, return
       else: emit error event, track telemetry, return
     }
  
  6. End turn:
     endTurn()
     this.activeTurn = null
     emit turn.ended event
     return TurnEndResult { event, stopReason }
```

## Tool Call Deduplication

`ToolCallDeduplicator` prevents re-executing identical tool calls:

```typescript
class ToolCallDeduplicator {
  /**
   * Check if a tool call was already executed this step or across steps.
   * Same-step: identical tool name + arguments within one LLM turn
   * Cross-step: identical tool name + arguments across multiple LLM turns
   */
  isDuplicate(toolName: string, args: string): boolean;

  /**
   * Record a tool call as executed.
   */
  record(toolName: string, args: string, result: unknown): void;

  /**
   * Get cached result for a duplicate tool call.
   */
  getCached(toolName: string, args: string): unknown;

  /**
   * Clear step-level cache (called at end of each step).
   */
  clearStepCache(): void;
}
```

## Loop Hooks Wiring

TurnFlow wires the stateless loop's hooks:

```typescript
const loopHooks: LoopHooks = {
  beforeStep: async (step) => {
    // 1. Flush steer buffer
    for (const steer of this._steerBuffer) {
      context.appendUserMessage(steer.input, steer.origin);
    }
    this._steerBuffer = [];
    
    // 2. Check compaction
    await fullCompaction.beforeStep(signal);
    
    // 3. Inject dynamic content
    await injection.inject();
    
    // 4. Fire hook
    return hooks.triggerBlock('PreToolUse', { step });
  },

  afterStep: async (step) => {
    await fullCompaction.afterStep();
    return hooks.trigger('PreToolUse', { step });
  },

  shouldContinueAfterStop: (stopReason) => {
    // Some stop reasons (max_tokens) can continue
    return stopReason === 'max_tokens' && hasBufferedSteer();
  },

  prepareToolExecution: async (toolCall) => {
    // Permission check
    return permission.beforeToolCall(toolCall);
  },

  finalizeToolResult: async (toolCall, result) => {
    return hooks.trigger('PostToolUse', { toolCall, result });
  },
};
```

## Error Handling During Turn

TurnFlow classifies API errors from the loop:

| Error | Behavior |
|-------|----------|
| `APIContextOverflowError` | Trigger compaction and retry |
| Rate limit (429) | Report to user, abort turn |
| Auth failure (401/403) | Report credential issue, abort turn |
| Network / timeout | Retry with backoff (via `chatWithRetry`) |
| `APIEmptyResponseError` | Retry once |
| Other 4xx/5xx | Report error, abort turn |

## Telemetry

TurnFlow tracks:
- `trackDuplicateToolCall()` — logs metrics when deduplication fires
- Turn duration
- Token usage per turn
- Error types per turn

## Re-implementation Notes

1. **prompt vs steer**: `prompt` starts a new turn (rejects if one is active). `steer` buffers input during an active turn. This distinction is important for UX — the user can type while the model is generating.

2. **Steer buffer flush timing**: Buffered inputs are flushed at the `beforeStep` hook, which fires before each LLM call. This means the model doesn't see steered input until the next step — it won't interrupt mid-response.

3. **AbortController pattern**: Each active turn creates a new `AbortController`. The controller's signal is passed through the entire call chain (loop → kosong generate → provider HTTP client). When `cancel()` is called, all in-flight HTTP requests are aborted.

4. **UserPromptSubmit hook can block**: Before any turn starts, the hook can inspect the user input and block it (e.g., for content moderation). The `markLastUserPromptBlocked()` method records this in the context.

5. **Compaction integration**: The `fullCompaction.beforeStep()` call checks whether context is approaching the token limit. If so, it initiates compaction (summarizing old messages) before the LLM call. This is transparent to the user.
