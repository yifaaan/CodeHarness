# Core Beliefs

Design principles that guide the Kimi Code CLI architecture.

## 1. Event Sourcing

**Principle**: All state changes are recorded as append-only events.

**Why**: Enables session replay, crash recovery, and debugging by inspecting the exact sequence of events.

**How**: Every action logs a record before execution. Records stored in `wire.jsonl` (JSON Lines format).

```
Agent action
    │
    ├── logRecord({ type: 'turn.prompt', input, origin })
    ├── Execute action
    ├── logRecord({ type: 'context.append_message', message })
    │
    └── All records → wire.jsonl

Session resume:
    records.replay()
    ├── Read all records from wire.jsonl
    ├── For each: restoreAgentRecord(agent, record)
    └── Agent state fully reconstructed
```

## 2. Two-Phase Tool Execution

**Principle**: Tools have pure validation phase before side-effectful execution.

**Why**: Enables permission checking before I/O, UI preview of actions, cancellation before execution.

**How**:
```typescript
interface ExecutableTool {
  // Phase 1: Pure validation, no side effects
  resolveExecution(args, context): ToolExecution;
  
  // Phase 2: Side-effectful execution
  execute(context): Promise<ToolResult>;
}
```

**Flow**:
```
Tool call requested
    │
    ▼
resolveExecution() ──> returns ToolExecution
    │                       ├── description (what will happen)
    │                       └── accesses (what resources accessed)
    ▼
PermissionManager.beforeToolCall()
    │
    ├── allow ──> execute()
    ├── deny ───> block
    └── ask ────> requestApproval() ──> user decides
```

## 3. Stateless Loop

**Principle**: The turn execution loop has no hidden state.

**Why**: Makes the loop testable, portable, and easy to reason about.

**How**:
```typescript
async function runTurn(input: {
  turnId: string;
  signal: AbortSignal;
  llm: LLM;
  buildMessages: () => Message[];
  dispatchEvent: LoopEventDispatcher;
  tools?: ExecutableTool[];
  hooks?: LoopHooks;
  maxSteps?: number;
}): Promise<TurnResult>
```

All dependencies injected. No global state.

## 4. Progressive Disclosure

**Principle**: Documentation organized from high-level to detailed.

**Why**: Smart agents need a map, not a 1000-page manual. Context is scarce.

**Structure**:
```
AGENTS.md (100 lines) → ARCHITECTURE.md (200 lines)
    │
    ▼
design-docs/ ──> Core beliefs, system overview
    │
    ▼
references/ ──> Module reference (150 lines each)
```

## 5. Repository as Source of Truth

**Principle**: Code repository is the only thing the agent can see.

**Why**: Knowledge in Google Docs, Slack, or human brains is invisible to the agent.

**How**:
- All design decisions documented in `design-docs/`
- All module interfaces in `references/`
- Plans in `exec-plans/` with decision logs

## 6. Unified Abstractions

**Principle**: Common interfaces for conceptually similar operations.

**Examples**:
- **Kaos**: Unified filesystem/process interface (local or SSH)
- **llm**: Unified LLM provider interface (Anthropic, OpenAI, etc.)
- **ChatProvider**: All providers implement same interface

## 7. Fail Open for Hooks

**Principle**: Hook failures don't block the agent.

**Why**: Prevents broken scripts from blocking the agent.

**How**: Hook commands receive JSON on stdin, return JSON on stdout. Non-zero exit = allow by default.

## 8. Context Compaction

**Principle**: Automatically summarize old context when approaching token limits.

**Why**: Prevents context overflow errors. Maintains conversation coherence.

**How**: At 75% of context limit, call LLM to summarize old messages, replace with summary.

## See Also

- [system-overview.md](system-overview.md) — System boundaries
- [data-flow.md](data-flow.md) — Request/response flows
