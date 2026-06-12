# Plan: Implement Session and RPC Layer

## Status
- **Phase**: Services
- **Priority**: Medium
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the session management system and RPC layer for communication between UI and agent core.

## Scope

### In Scope
- Implement Session class
- Implement SessionStore for persistence
- Implement RPC protocol
- Add session lifecycle management
- Add unit tests

### Out of Scope
- Multi-agent sessions (future phase)
- Session sharing (future phase)
- Session optimization (future phase)

## Implementation Steps

### Week 1: Session Management

1. **Define Session Types**
   - Create `src/codeharness/engine/session_types.h`
   - Define `SessionConfig`, `SessionState` types
   - Define `SessionInfo` for session metadata
   - Define `SessionStore` interface

2. **Implement Session Class**
   - Create `src/codeharness/engine/session.h` and `session.cpp`
   - Implement session creation and resumption
   - Implement session close and cleanup
   - Implement session metadata management

3. **Create Unit Tests**
   - Create `tests/engine/session_tests.cpp`
   - Test session creation
   - Test session resumption
   - Test session close

### Week 2: RPC and Persistence

4. **Implement RPC Protocol**
   - Create `src/codeharness/engine/rpc.h` and `rpc.cpp`
   - Implement RPC message types
   - Implement RPC server and client
   - Implement RPC error handling

5. **Implement Session Store**
   - Create `src/codeharness/engine/session_store.h` and `session_store.cpp`
   - Implement file-based persistence
   - Implement session index
   - Implement session locking

6. **Integration Testing**
   - Create integration tests with mock agent
   - Test complete session lifecycle
   - Test RPC communication
   - Test session persistence

7. **Documentation**
   - Update architecture docs to include Session
   - Add API reference for Session
   - Add RPC protocol guide

## Dependencies

- **Agent**: Required for agent integration
- **Records**: Required for event sourcing
- **Kaos**: Required for file I/O

## Success Criteria

- [ ] Session class implemented and tested
- [ ] SessionStore implemented with persistence
- [ ] RPC protocol working correctly
- [ ] Session lifecycle management working
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| RPC complexity | Medium | Use well-tested patterns, comprehensive logging |
| Session corruption | High | Checksums, atomic writes, backup |
| Concurrency issues | Medium | Use locks, document thread safety |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Session Isolation**: Sessions are independent
2. **Atomic Persistence**: Session writes are atomic
3. **RPC Reliability**: RPC messages are delivered
4. **Clean Shutdown**: Sessions close cleanly

## References

- [Kimi Code Session](../../kimi-code-analysis/core-components.md#8-session-management)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
