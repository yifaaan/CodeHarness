# Agent Records and Replay

**Source**: `packages/agent-core/src/agent/records/`, `packages/agent-core/src/agent/replay/`

## Purpose

The records system implements **event sourcing** — every state-changing operation in the agent is recorded as an append-only event. These events are persisted to `wire.jsonl`, enabling:

1. **Session resume**: Reconstruct agent state from previous sessions
2. **Crash recovery**: Recover from crashes without data loss
3. **Debugging**: Inspect the exact sequence of events that led to any state
4. **Session export**: Package the event log for sharing or analysis

## Event Sourcing Architecture

```
Agent action (method call)
    │
    ├── logRecord({ type: 'turn.prompt', input, origin })
    ├── Execute the action
    ├── logRecord({ type: 'context.append_message', message })
    ├── logRecord({ type: 'config.update', ... })
    │
    └── All records → FileSystemAgentRecordPersistence → wire.jsonl

Session resume:
    records.replay()
    │
    ├── Read all records from wire.jsonl
    ├── For each record: restoreAgentRecord(agent, record)
    │   ├── turn.prompt → agent.turn.restorePrompt(...)
    │   ├── context.append_message → agent.context.appendMessage(...)
    │   ├── config.update → agent.config.applyUpdate(...)
    │   └── ... 20+ record types
    │
    └── Agent state fully reconstructed
```

## AgentRecords

**Source**: `packages/agent-core/src/agent/records/index.ts`

```typescript
class AgentRecords {
  constructor(config: RecordsConfig);

  /** Record an event (persists and restores state) */
  logRecord(record: AgentRecord): void;

  /** Replay all persisted records to restore state */
  replay(): Promise<void>;

  /** Is the agent currently replaying? (restoring — no UI events) */
  get restoring(): boolean;
  
  /** Callback for SDK consumers to observe records */
  onRecord?: (record: AgentRecord) => void;

  /** Flush pending records to disk */
  flush(): Promise<void>;

  /** Close the persistence layer */
  close(): Promise<void>;
}

interface RecordsConfig {
  persistence: AgentRecordPersistence;
  persist: boolean;   // If false, events are logged but not persisted (testing)
}
```

## AgentRecord Types (20+)

Every state change has a corresponding record type:

### Turn Records
```typescript
{ type: 'turn.prompt'; input: ContentPart[]; origin: PromptOrigin }
{ type: 'turn.steer'; input: ContentPart[]; origin: PromptOrigin }
{ type: 'turn.cancel' }
```

### Config Records
```typescript
{ type: 'config.update'; patch: Partial<AgentConfigData> }
  // Patches config.systemPrompt, config.model, config.thinkingLevel, etc.
```

### Permission Records
```typescript
{ type: 'permission.set_mode'; mode: PermissionMode }
{ type: 'permission.record_approval_result'; result: ApprovalResult }
```

### Context Records
```typescript
{ type: 'context.append_message'; message: ContextMessage }
{ type: 'context.append_loop_event'; event: LoopRecordedEvent }
{ type: 'context.apply_compaction'; result: CompactionRecord }
{ type: 'context.clear' }
{ type: 'context.mark_last_user_prompt_blocked' }
```

### Compaction Records
```typescript
{ type: 'full_compaction.begin'; data?: { instruction?: string } }
{ type: 'full_compaction.cancel' }
{ type: 'full_compaction.complete'; result: CompactionRecord }
```

### Plan Mode Records
```typescript
{ type: 'plan_mode.enter'; planId: string }
{ type: 'plan_mode.cancel' }
{ type: 'plan_mode.exit' }
```

### Tool Records
```typescript
{ type: 'tools.register_user_tool'; tool: UserToolDefinition }
{ type: 'tools.unregister_user_tool'; name: string }
{ type: 'tools.set_active_tools'; tools: string[] }
{ type: 'tools.update_store'; key: string; value: unknown }
```

### Background & Usage Records
```typescript
{ type: 'background.stop'; taskId: string }
{ type: 'usage.record'; usage: TokenUsage }
```

## Wire.jsonl Format

```jsonl
{"type":"turn.prompt","input":[{"type":"text","text":"帮我梳理这个项目的架构"}],"origin":"user","ts":1718190000000}
{"type":"context.append_message","message":{"role":"user","content":[...],"origin":"user","tokenCount":12,"id":"msg_1","createdAt":"..."},"ts":1718190001000}
{"type":"config.update","patch":{"systemPrompt":"You are a coding assistant..."},"ts":1718190002000}
{"type":"full_compaction.complete","result":{"summary":"User requested architecture review...","tokenCount":85,"messageCount":15},"ts":1718190010000}
```

Each line is a JSON object with:
- `type`: Record type (discriminant)
- Record-specific fields
- `ts`: Timestamp (milliseconds)

### File Format

```
wire.jsonl
├── Append-only JSON Lines (newline-delimited JSON)
├── No indexing, no mutations, no deletions
├── Protocol version: "1.0" (stored in session metadata)
└── File path: <agentHomedir>/wire.jsonl
```

## Persistence Layer

**Source**: `packages/agent-core/src/agent/records/`

```typescript
interface AgentRecordPersistence {
  /** Read all records (for replay) */
  read(): AsyncIterable<AgentRecord>;

  /** Append a single record */
  append(record: AgentRecord): Promise<void>;

  /** Flush buffers to disk */
  flush(): Promise<void>;

  /** Close and release resources */
  close(): Promise<void>;
}

class FileSystemAgentRecordPersistence implements AgentRecordPersistence {
  constructor(filePath: string, kaos: Kaos);
  
  // Writes to wire.jsonl via Kaos.writeText (append mode)
  // Buffers writes for performance (flush on turn boundaries)
}
```

## Replay System

**Source**: `packages/agent-core/src/agent/records/replay.ts`

```typescript
/**
 * Restore agent state from a single record.
 * This is the big "switch" that reconstructs state.
 * 
 * CRITICAL: Must NOT emit UI events, call LLM, execute tools,
 * or cause side effects. This is state reconstruction only.
 */
function restoreAgentRecord(agent: Agent, record: AgentRecord): void {
  switch (record.type) {
    case 'turn.prompt':
      agent.turn.restorePrompt(record.input, record.origin);
      break;
      
    case 'turn.steer':
      agent.turn.restoreSteer(record.input, record.origin);
      break;
      
    case 'turn.cancel':
      agent.turn.cancel();  // Only if actively restoring
      break;
      
    case 'config.update':
      agent.config.applyUpdate(record.patch);
      break;
      
    case 'permission.set_mode':
      agent.permission.setMode(record.mode);
      break;
      
    case 'context.append_message':
      agent.context.appendMessage(record.message, { restore: true });
      break;
      
    case 'context.append_loop_event':
      agent.context.appendLoopEvent(record.event, { restore: true });
      break;
      
    case 'context.apply_compaction':
      agent.context.applyCompaction(record.result, { restore: true });
      break;
      
    case 'context.clear':
      agent.context.clear();
      agent.injection.onContextClear();
      break;
      
    case 'full_compaction.begin':
      agent.fullCompaction.begin(record.data, { restore: true });
      break;
      
    case 'full_compaction.complete':
      agent.fullCompaction.complete(record.result, { restore: true });
      break;
      
    case 'plan_mode.enter':
      agent.planMode.restoreEnter(record.planId);
      break;
      
    case 'plan_mode.exit':
      agent.planMode.restoreExit();
      break;
      
    case 'tools.register_user_tool':
      agent.tools.registerUserTool(record.tool, { restore: true });
      break;
      
    case 'tools.set_active_tools':
      agent.tools.setActiveTools(record.tools, { restore: true });
      break;
      
    case 'usage.record':
      agent.usage.record(record.usage, { restore: true });
      break;
      
    case 'background.stop':
      agent.background.stop(record.taskId, { restore: true });
      break;
      
    // ... more cases
  }
}
```

### Replay Flow

```
Agent.resume():
  1. Set agent.records.restoring = true
     → All subsystems check this flag and suppress UI events
  
  2. replay():
     for each record in persistence.read():
       restoreAgentRecord(agent, record)
     
     → Agent state matches what it was when the session ended
  
  3. Load background manager:
     background.reconcile()
     → Check for lost background tasks
     → Mark orphaned tasks as 'lost'
  
  4. Turn flow:
     turn.finishResume()
     → Clear 'resuming' state
     → Ready for new user input
  
  5. Set agent.records.restoring = false
```

## ReplayBuilder

**Source**: `packages/agent-core/src/agent/replay/index.ts`

```typescript
class ReplayBuilder {
  /** Capture a record for export (not persisted to wire.jsonl) */
  capture(record: AgentRecord): void;

  /** Build the final replay result */
  buildResult(): AgentReplayRecord[];
}

interface AgentReplayRecord {
  type: string;
  data: unknown;
  ts: number;
}
```

Used for **session export** — captures the final state of a session for packaging into a ZIP file. The export includes all agent records plus session metadata.

## Session Export

```typescript
class SessionExport {
  constructor(session: Session);

  /** Export session as ZIP bytes */
  async export(): Promise<Uint8Array>;

  /** Export session to file */
  async exportToFile(path: string): Promise<void>;
}
```

Export format (ZIP):
```
export.zip
├── state.json              # Session metadata
├── agents/
│   ├── main/
│   │   └── wire.jsonl      # Full event log
│   └── <subagent>/
│       └── wire.jsonl
└── manifest.json           # Export manifest (version, date, agent count)
```

## Re-implementation Notes

1. **wire.jsonl is the most portable format**: It's JSON Lines — every language can read and write it. No indexing, no mutations, append-only. A session is fully reconstructable from its wire.jsonl file.

2. **restoreAgentRecord() must be side-effect-free**: During replay, the agent is restoring state, not executing actions. The function must NOT:
   - Emit UI events (the `restoring` flag suppresses this)
   - Call the LLM
   - Execute tools
   - Make network requests

3. **Protocol versioning**: The `AGENT_WIRE_PROTOCOL_VERSION = '1.0'` is stored in session metadata. Future versions can add new record types or change field structures. Replay should handle unknown record types gracefully (skip them).

4. **The restoring flag is checked throughout**: Every subsystem checks `agent.records.restoring` before emitting events. This is how the agent knows whether it's live or replaying.

5. **Records are not the only persistence**: The session also has `state.json` (title, metadata) and `session_index.jsonl` (fast lookup). wire.jsonl is the source of truth for agent state; the other files are auxiliary.

6. **Performance**: For long sessions, wire.jsonl can grow large. The replay reads all records linearly — O(n) time. Buffered writes (flush on turn boundaries) prevent excessive disk I/O during active conversations.