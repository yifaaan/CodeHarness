# Plan: Implement Agent Turn Flow

## Status
- **Phase**: Agent Core
- **Priority**: Medium
- **Estimated Effort**: 1 week
- **Owner**: Core Team

## Objective

Implement the TurnFlow class that manages the complete lifecycle of a user turn, including input handling, steering, and cancellation.

## Scope

### In Scope
- Implement TurnFlow class
- Implement prompt/steer/cancel methods
- Implement steer buffer for mid-turn input
- Add turn lifecycle hooks
- Add unit tests

### Out of Scope
- Multi-turn conversations (future phase)
- Turn persistence (future phase)
- Turn optimization (future phase)

## Implementation Steps

### Week 1: TurnFlow Implementation

1. **Define Turn Types**
   - Create `src/codeharness/engine/turn_types.h`
   - Define `TurnState`, `TurnConfig` types
   - Define `TurnResult`, `TurnError` types
   - Define `SteerBuffer` for mid-turn input

2. **Implement TurnFlow Class**
   - Create `src/codeharness/engine/turn.h` and `turn.cpp`
   - Implement `prompt` method for new user input
   - Implement `steer` method for mid-turn input
   - Implement `cancel` method for aborting turns

3. **Create Unit Tests**
   - Create `tests/engine/turn_tests.cpp`
   - Test prompt flow
   - Test steer buffering
   - Test cancel handling

### Additional Tasks

4. **Implement Turn Worker**
   - Implement `turnWorker` internal method
   - Implement UserPromptSubmit hook
   - Implement error handling and recovery
   - Implement turn completion

5. **Integration Testing**
   - Create integration tests with mock agent
   - Test complete turn lifecycle
   - Test steer buffer behavior
   - Test cancel scenarios

6. **Documentation**
   - Update architecture docs to include TurnFlow
   - Add API reference for TurnFlow
   - Add turn lifecycle guide

## Dependencies

- **Agent**: Required for agent integration
- **Loop**: Required for turn execution

## Success Criteria

- [ ] TurnFlow class implemented and tested
- [ ] Prompt/steer/cancel methods working
- [ ] Steer buffer implemented correctly
- [ ] Turn lifecycle hooks working
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Steer buffer complexity | Medium | Use simple queue, add comprehensive tests |
| Cancel handling | High | Ensure clean cancellation, proper resource cleanup |
| Turn state management | Medium | Use explicit state machine, document transitions |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Turn Isolation**: Each turn is independent
2. **Steer Safety**: Steer does not corrupt turn state
3. **Clean Cancel**: Cancel releases all resources
4. **State Machine**: Turn state transitions are explicit

## References

- [Kimi Code TurnFlow](../../kimi-code-analysis/core-components.md#3-turnflow)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
