# Agent Records and Replay (Event Sourcing)

## Core Concept

**Event Sourcing**: Every state change is recorded as an append-only event in `wire.jsonl`.

## AgentRecords

```typescript
class AgentRecords {
  logRecord(record: AgentRecord): void;    // Record event
  replay(): Promise<void>;                  // Restore state from records
  get restoring(): boolean;                 // Is replaying?
  flush(): Promise<void>;                   // Flush to disk
}
```

## Record Types (20+)

| Category | Types |
|----------|-------|
| **Turn** | `turn.prompt`, `turn.steer`, `turn.cancel` |
| **Config** | `config.update` |
| **Permission** | `permission.set_mode`, `permission.record_approval_result` |
| **Context** | `context.append_message`, `context.append_loop_event`, `context.apply_compaction`, `context.clear` |
| **Compaction** | `full_compaction.begin`, `full_compaction.complete`, `full_compaction.cancel` |
| **Plan Mode** | `plan_mode.enter`, `plan_mode.exit`, `plan_mode.cancel` |
| **Tools** | `tools.register_user_tool`, `tools.unregister_user_tool`, `tools.set_active_tools` |
| **Background** | `background.stop` |
| **Usage** | `usage.record` |

## Wire.jsonl Format

```jsonl
{"type":"turn.prompt","input":[{"type":"text","text":"..."}],"origin":"user","ts":1718190000000}
{"type":"context.append_message","message":{"role":"user","content":[...]},"ts":1718190001000}
```

- Append-only JSON Lines
- No indexing, no mutations, no deletions
- Protocol version: "1.0"

## Persistence

```typescript
interface AgentRecordPersistence {
  read(): AsyncIterable<AgentRecord>;    // For replay
  append(record: AgentRecord): Promise<void>;
  flush(): Promise<void>;
  close(): Promise<void>;
}
```

## Replay

```typescript
function restoreAgentRecord(agent: Agent, record: AgentRecord): void {
  // Restores state without side effects
  // No UI events, no LLM calls, no tool execution
}
```

## Key Invariants

1. **No side effects during replay**: `restoring` flag disables event emission
2. **Append-only**: Records cannot be modified or deleted
3. **Ordered**: Records are replayed in order
4. **Complete**: All state changes must be recorded

## File Path

```
~/.kimi-code/sessions/<workdir-key>/<session-id>/agents/<agent-id>/wire.jsonl
```

---

**See also**: [14-agent-records-and-replay.md](../14-agent-records-and-replay.md) for full details.
