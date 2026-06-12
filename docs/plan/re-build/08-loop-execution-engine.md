# Loop Execution Engine

**Source**: `packages/agent-core/src/loop/`

## Purpose

The loop module contains the **stateless turn execution engine** — the decision-making core that orchestrates the LLM-tool interaction cycle. It takes a turn's context, calls the LLM, executes any tool calls, and repeats until the LLM signals completion.

The loop is **stateless** — all dependencies are injected as parameters. This makes it testable in isolation and portable across environments.

## Architecture

```
runTurn(input)
    │
    ▼
┌────────────────────────────────────────────────────────────────┐
│  Step Loop (maxSteps = 1000)                                    │
│                                                                  │
│  repeat:                                                         │
│    beforeStep() ──> executeLoopStep() ──> afterStep()            │
│                          │                                       │
│               ┌──────────┴──────────┐                           │
│               ▼                     ▼                           │
│         LLM.chat()             tool execution                   │
│               │                     │                           │
│               ▼                     ▼                           │
│         streamed response      ToolResults                     │
│               │                     │                           │
│               ▼                     ▼                           │
│         stopReason:            stopReason:                      │
│         end_turn ───> EXIT    tool_use ───> continue            │
│         max_tokens ──> check_hooks                              │
│         filtered ──> EXIT                                       │
│         paused ──> EXIT                                         │
│                                                                 │
│  until terminal stop reason or maxSteps exceeded                │
└────────────────────────────────────────────────────────────────┘
```

## Core Function: runTurn

**Source**: `packages/agent-core/src/loop/run-turn.ts`

```typescript
async function runTurn(input: {
  turnId: string;
  signal: AbortSignal;
  llm: LLM;
  buildMessages: () => Message[];
  dispatchEvent: LoopEventDispatcher;
  tools?: ExecutableTool[];
  hooks?: LoopHooks;
  maxSteps?: number;           // default 1000
}): Promise<TurnResult>
```

### Parameters

| Parameter | Type | Purpose |
|-----------|------|---------|
| `turnId` | `string` | Unique turn identifier |
| `signal` | `AbortSignal` | Cancellation signal (from TurnFlow) |
| `llm` | `LLM` | LLM interface wrapping a kosong ChatProvider |
| `buildMessages` | `() => Message[]` | Lazily builds message list from context |
| `dispatchEvent` | `LoopEventDispatcher` | Emits events (recorded + live) |
| `tools` | `ExecutableTool[]` | Tools available to the LLM this turn |
| `hooks` | `LoopHooks` | Lifecycle callbacks |
| `maxSteps` | `number` | Maximum LLM-tool iterations |

### Return Value

```typescript
interface TurnResult {
  stopReason: LoopTurnStopReason;  // 'end_turn' | 'tool_use' | 'max_tokens' | 'filtered' | 'paused' | 'max_steps'
  usage: TokenUsage;               // Aggregated across all steps
  steps: number;                   // Number of steps executed
}
```

### Step Loop Algorithm

```
async function runTurn(input):
  step = 0
  stopReason = null
  totalUsage = empty
  
  while stopReason is null and step < maxSteps (1000):
    step += 1
    
    // 1. Before step hook
    hookResult = await hooks.beforeStep(step)
    if hookResult?.action === 'block':
      stopReason = 'blocked'
      break
    
    // 2. Check abort
    signal.throwIfAborted()
    
    // 3. Execute step
    stepResult = await executeLoopStep({
      step,
      llm, signal,
      buildMessages,
      dispatchEvent,
      tools,
      hooks,
    })
    
    // 4. After step hook
    await hooks.afterStep(step)
    
    // 5. Accumulate usage
    totalUsage = addUsage(totalUsage, stepResult.usage)
    
    // 6. Determine whether to continue
    switch stepResult.stopReason:
      case 'tool_use':     → continue loop
      case 'end_turn':     → stopReason = 'end_turn', break
      case 'max_tokens':
        continue = await hooks.shouldContinueAfterStop('max_tokens')
        if continue: → continue loop
        else: stopReason = 'max_tokens', break
      case 'filtered':     → stopReason = 'filtered', break
      case 'paused':       → stopReason = 'paused', break
  
  if step >= maxSteps:
    throw KimiError(ErrorCodes.LOOP_MAX_STEPS_EXCEEDED)
  
  return { stopReason, usage: totalUsage, steps }
```

## Step Execution: executeLoopStep

**Source**: `packages/agent-core/src/loop/turn-step.ts`

```
executeLoopStep({step, llm, signal, buildMessages, dispatchEvent, tools, hooks}):
  
  1. Emit step.started event
  
  2. Call LLM:
     messages = buildMessages()   // Get current conversation from context
     
     response = await chatWithRetry(llm, {
       systemPrompt: llm.systemPrompt,
       tools: tools.map(t => t.schema),  // Only send tool schemas, not implementations
       messages,
       signal,
       callbacks: {
         onTextDelta(text)  → emit assistant.delta
         onThinkDelta(think) → emit thinking.delta
         onToolCallDelta(call) → accumulate tool call
         onTextPart(part)   → final text part (non-streaming providers)
         onThinkPart(part)  → final think part
       }
     })
  
  3. If no tool calls in response:
     emit step.end with stopReason = response.finishReason
     return { stopReason: response.finishReason, usage: response.usage }
  
  4. If tool calls present:
     results = await runToolCallBatch({
       toolCalls: response.toolCalls,
       tools,
       signal,
       hooks,
       dispatchEvent,
     })
     
     Append tool results to transcript (via dispatchEvent)
     
     emit step.end
     return { stopReason: 'tool_use', usage: response.usage }
```

## LLM Interface

```typescript
interface LLM {
  /** The system prompt being used */
  readonly systemPrompt: string;
  
  /** Model name for display/debugging */
  readonly modelName: string;
  
  /** Model capabilities (vision, thinking, tool use) */
  readonly capability: ModelCapability;

  /**
   * Send messages to the LLM and get a response.
   * Returns a structured response with content and tool calls.
   */
  chat(params: LLMChatParams): Promise<LLMChatResponse>;
  
  /**
   * Check if an error is retryable (rate limits, timeouts).
   */
  isRetryableError(error: unknown): boolean;
}

interface LLMChatParams {
  systemPrompt: string;
  tools: Tool[];
  messages: Message[];
  signal?: AbortSignal;
  callbacks?: {
    onTextDelta?: (text: string) => void;
    onThinkDelta?: (think: string) => void;
    onToolCallDelta?: (toolCall: Partial<ToolCall>) => void;
    onTextPart?: (part: TextPart) => void;
    onThinkPart?: (part: ThinkPart) => void;
  };
}

interface LLMChatResponse {
  content: ContentPart[];
  toolCalls: ToolCall[];
  finishReason: FinishReason;
  usage: TokenUsage;
}
```

### KosongLLM

The concrete implementation wraps kosong's `ChatProvider` and `generate`:

```typescript
class KosongLLM implements LLM {
  constructor(
    private provider: ChatProvider,
    private systemPrompt: string,
    private authResolver: ProviderRequestAuthResolver,
  );
  
  async chat(params: LLMChatParams): Promise<LLMChatResponse> {
    const auth = this.authResolver(this.provider.modelName);
    const stream = await this.provider.generate(
      params.systemPrompt,
      params.tools,
      params.messages,
      { signal: params.signal, auth: auth ?? undefined },
    );
    
    // Accumulate streamed response
    const content: ContentPart[] = [];
    const toolCalls: ToolCall[] = [];
    
    for await (const part of stream) {
      if (part.type === 'text') content.push(part);
      else if (part.type === 'think') content.push(part);
      else if (part.type === 'function') {
        // Merge with existing tool call or add new one
        mergeToolCall(toolCalls, part);
      }
      // Call streaming callbacks
      params.callbacks?.onTextDelta?.(part.text);
    }
    
    return {
      content,
      toolCalls,
      finishReason: stream.finishReason,
      usage: stream.usage,
    };
  }
}
```

## Tool Execution: runToolCallBatch

**Source**: `packages/agent-core/src/loop/tool-scheduler.ts`

```
runToolCallBatch({toolCalls, tools, signal, hooks, dispatchEvent}):
  
  results = []
  
  for each toolCall in toolCalls:
  
    1. Find matching tool in tools[] by name:
       tool = tools.find(t => t.name === toolCall.function.name)
       if not found → error: "Unknown tool: {name}"
    
    2. Prepare tool execution (permission check + pre-execution hook):
       hookResult = hooks.prepareToolExecution(toolCall)
       if hookResult?.action === 'block':
         record blocked tool result
         continue  // skip this tool
       
       if hookResult?.action === 'allow':  
         (proceed — permission granted)
    
    3. Check deduplication:
       if ToolCallDeduplicator.isDuplicate(toolCall.function.name, args):
         result = ToolCallDeduplicator.getCached(name, args)
         dispatch cached result
         continue
    
    4. Resolve execution:
       execution = tool.resolveExecution(parsedArgs)
       // execution.description — human-readable summary for UI
       // execution.accesses — what resources are accessed
    
    5. Execute:
       emit tool.call.started
       
       result = await execution.execute({
         turnId,
         toolCallId: toolCall.id,
         signal,
         onUpdate: (update) => emit tool.progress,
       })
       
       emit tool.result
    
    6. Finalize:
       hooks.finalizeToolResult(toolCall, result)
       ToolCallDeduplicator.record(toolCall.function.name, args, result)
       results.push(result)
  
  return results
```

## Retry Logic: chatWithRetry

```
chatWithRetry(llm, params):
  maxAttempts = 3
  baseDelay = 1000  // 1 second
  
  for attempt = 1 to maxAttempts:
    try:
      return await llm.chat(params)
    catch error:
      if not llm.isRetryableError(error): throw
      if attempt == maxAttempts: throw
      
      delay = baseDelay * 2^(attempt - 1)  // exponential backoff
      jitter = random(0, delay * 0.1)      // 10% jitter
      await sleep(delay + jitter)
```

## LoopHooks Interface

```typescript
interface LoopHooks {
  /** Called before each step. Can block execution. */
  beforeStep?(step: number): Promise<HookResult | void>;
  
  /** Called after each step completes. */
  afterStep?(step: number): Promise<void>;
  
  /** Called when LLM returns max_tokens. Can continue the loop. */
  shouldContinueAfterStop?(stopReason: string): Promise<boolean>;
  
  /** Called before each tool execution. Permission check + pre-execution. */
  prepareToolExecution?(toolCall: ToolCall): Promise<HookResult | void>;
  
  /** Called after each tool result is received. */
  finalizeToolResult?(toolCall: ToolCall, result: ToolResult): Promise<void>;
}
```

## LoopEventDispatcher

```typescript
interface LoopEventDispatcher {
  /** Emit an event that is BOTH recorded (persisted to wire.jsonl) AND live (UI) */
  recorded<T extends AgentEvent>(event: T): void;
  
  /** Emit an event that is ONLY live (streaming deltas, tool progress) */
  live<T extends AgentEvent>(event: T): void;
}
```

The distinction matters:
- **Recorded events**: Persisted to wire.jsonl for session replay. These are structural events like turn start/end, tool results, context changes.
- **Live events**: Only streamed to the UI during active conversation. These are transient — streaming deltas, tool progress updates, thinking tokens. Not persisted because they would make wire.jsonl enormous and replay would regenerate them.

## Event Flow During a Step

```
step.begin (recorded)
  │
  ├── assistant.delta (live)         ← text tokens as they arrive
  ├── thinking.delta (live)           ← thinking tokens as they arrive
  ├── tool.call.started (recorded)    ← tool call discovered
  ├── tool.progress (live)            ← tool execution updates
  └── tool.result (recorded)          ← tool execution result
  │
step.end (recorded)
```

## Re-implementation Notes

1. **The loop is a pure function**: `runTurn()` has no hidden state — all dependencies are injected. This is the most portable module in the entire system. Implement it as a function, not a class.

2. **The maxSteps (1000) limit prevents infinite loops**: Without it, an LLM that keeps calling tools would run forever. The `LOOP_MAX_STEPS_EXCEEDED` error forces the agent to wrap up.

3. **Two-phase tool execution**: `tool.resolveExecution(args)` returns a `ToolExecution` with an `execute()` method. The resolve phase is pure — it validates args and returns metadata. The execute phase is side-effectful. This separation enables:
   - Permission checking before execution (permission manager sees the args)
   - UI rendering of what will happen before it happens
   - Cancellation before I/O

4. **chatWithRetry**: Only retries on transient errors (rate limits, timeouts, network errors). Non-retryable errors (auth failures, context overflow, invalid requests) are thrown immediately.

5. **Event dispatch separation**: The recorded/live event distinction must be preserved. Recorded events make wire.jsonl replayable. Live events make the UI responsive. Don't persist live events — they're too numerous and replayable from recorded events.
