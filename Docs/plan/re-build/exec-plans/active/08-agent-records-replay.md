# Plan: Implement Agent Records and Replay

## Status
- **Phase**: Services
- **Priority**: Medium
- **Estimated Effort**: 1 week
- **Owner**: Core Team

## Objective

Implement the event sourcing system for recording agent state changes and replaying them for session recovery.

## Scope

### In Scope
- Implement AgentRecords class
- Implement record types for all state changes
- Implement wire.jsonl persistence
- Implement replay system
- Add unit tests

### Out of Scope
- Session export (future phase)
- Advanced replay features (future phase)
- Performance optimization (future phase)

## Implementation Steps

### Week 1: Records and Replay

1. **Define Record Types**
   - Create `src/codeharness/engine/record_types.h`
   - Define `AgentRecord` variant type
   - Define all 20+ record types
   - Define `RecordPersistence` interface

2. **Implement AgentRecords Class**
   - Create `src/codeharness/engine/records.h` and `records.cpp`
   - Implement `logRecord` method
   - Implement `replay` method
   - Implement `flush` and `close` methods

3. **Implement File Persistence**
   - Create `src/codeharness/engine/file_persistence.h` and `file_persistence.cpp`
   - Implement wire.jsonl append
   - Implement buffered writes
   - Implement file locking

4. **Implement Replay System**
   - Create `src/codeharness/engine/replay.h` and `replay.cpp`
   - Implement `restoreAgentRecord` function
   - Implement record type switch
   - Implement replay error handling

5. **Create Unit Tests**
   - Create `tests/engine/records_tests.cpp`
   - Test record logging
   - Test replay restoration
   - Test file persistence

6. **Documentation**
   - Update architecture docs to include Records
   - Add API reference for AgentRecords
   - Add wire.jsonl format documentation

## Dependencies

- **Agent**: Required for agent state access
- **Kaos**: Required for file I/O

## Success Criteria

- [ ] AgentRecords class implemented and tested
- [ ] All 20+ record types defined
- [ ] File persistence working
- [ ] Replay system working
- [ ] All unit tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Record completeness | High | Comprehensive testing, code review |
| Replay correctness | High | Property-based testing, replay verification |
| File corruption | Medium | Checksums, backup files |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Append-Only**: Records cannot be modified or deleted
2. **Complete Recording**: All state changes must be recorded
3. **Replay Safety**: Replay does not cause side effects
4. **Atomic Writes**: File writes are atomic

## References

- [Kimi Code Records](../../kimi-code-analysis/core-components.md#14-agent-records-and-replay)
- [Event Sourcing Pattern](https://martinfowler.com/eaaDev/EventSourcing.html)
