# Loop Engine

Turn execution loop.

## One-Liner

Stateless function that orchestrates the LLM-tool interaction cycle: call LLM, execute tools, repeat until completion.

## Core Function

```typescript
async function runTurn(input: {
  turnId: string;
  signal: AbortSignal;
  llm: LLM;
  buildMessages: () => Message[];
  dispatchEvent: LoopEventDispatcher;
  tools?: ExecutableTool[];
  hooks?: LoopHooks;
  maxSteps?: number;  // default 1000
}): Promise<TurnResult>
```

## Step Loop Algorithm

```
step = 0
while step < maxSteps:
    step++
    
    beforeStep hook
    signal.throwIfAborted()
    
    stepResult = await executeLoopStep({...})
    
    afterStep hook
    accumulate usage
    
    switch stepResult.stopReason:
        case 'tool_use': continue
        case 'end_turn': exit
        case 'max_tokens': check shouldContinueAfterStop
```

## Step Execution

```
executeLoopStep():
    1. Emit step.started
    2. Call LLM.chat() with streaming callbacks
       - onTextDelta → emit assistant.delta
       - onThinkDelta → emit thinking.delta
       - onToolCallDelta → accumulate tool call
    3. If no tool calls → return stopReason
    4. If tool calls → runToolCallBatch() → return 'tool_use'
```

## Tool Call Batch

```
runToolCallBatch():
    for each toolCall:
        1. Find matching tool
        2. prepareToolExecution hook (permission check)
        3. Check deduplication
        4. Emit tool.call.started
        5. Execute tool
        6. Emit tool.result
        7. finalizeToolResult hook
```

## LoopHooks Interface

```typescript
interface LoopHooks {
  beforeStep?(step: number): Promise<HookResult | void>;
  afterStep?(step: number): Promise<void>;
  shouldContinueAfterStop?(stopReason: string): Promise<boolean>;
  prepareToolExecution?(toolCall: ToolCall): Promise<HookResult | void>;
  finalizeToolResult?(toolCall: ToolCall, result: ToolResult): Promise<void>;
}
```

## Event Dispatch

```typescript
interface LoopEventDispatcher {
  recorded<T extends AgentEvent>(event: T): void;  // Persisted to wire.jsonl
  live<T extends AgentEvent>(event: T): void;     // Only to UI
}
```

## Retry Logic

```
chatWithRetry():
    maxAttempts = 3
    baseDelay = 1000ms
    
    for attempt = 1 to maxAttempts:
        try: return await llm.chat(params)
        catch error:
            if not retryable: throw
            if attempt == maxAttempts: throw
            delay = baseDelay * 2^(attempt - 1) + jitter
            await sleep(delay)
```

## Key Design

1. **Statelessness**: All dependencies injected. No hidden state.
2. **Max Steps**: 1000 step limit prevents infinite loops.
3. **AbortSignal**: Passed through entire chain for cancellation.

## See Also

- [agent-lifecycle.md](agent-lifecycle.md) — TurnFlow integration
- [tool-system.md](tool-system.md) — Tool execution
