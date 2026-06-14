# Plan: Implement Loop Execution Engine

## Status
- **Phase**: Agent Core
- **Priority**: High
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the stateless loop execution engine that drives the agent's turn-based interaction with LLMs and tools.

## Scope

### In Scope
- Implement `runTurn` stateless loop function
- Implement step execution with retry logic
- Implement tool call batch processing
- Add loop hooks for extension points
- Add unit tests

### Out of Scope
- Tool scheduler for concurrent execution (future phase)
- Advanced retry strategies (future phase)
- Loop performance optimization (future phase)

## Implementation Steps

### Week 1: Core Loop Implementation

1. **Define Loop Types**
   - Create `src/codeharness/engine/loop_types.h`
   - Define `TurnInput`, `TurnResult`, `LoopEvent` types
   - Define `LoopHooks` interface
   - Define `LoopEventDispatcher` type

2. **Implement runTurn Function**
   - Create `src/codeharness/engine/loop.h` and `loop.cpp`
   - Implement main loop with while(true) pattern
   - Implement step execution with maxSteps check
   - Implement abort signal handling

3. **Create Unit Tests**
   - Create `tests/engine/loop_tests.cpp`
   - Test basic turn execution
   - Test tool call handling
   - Test maxSteps limit
   - Test abort signal handling

### Week 2: Tool Execution and Error Handling

4. **Implement Tool Call Batch Processing**
   - Add tool call parsing from LLM response
   - Implement tool call validation
   - Implement tool execution with timeout
   - Implement tool result backfill

5. **Implement Retry Logic**
   - Add exponential backoff for transient errors
   - Add retry detection for failed tool calls
   - Add circuit breaker for repeated failures

6. **Integration Testing**
   - Create integration tests with mock LLM
   - Test complete turn flow
   - Test error recovery scenarios
   - Test concurrent tool calls

7. **Documentation**
   - Update architecture docs to include Loop
   - Add API reference for runTurn
   - Add loop extension guide

## Dependencies

- **llm**: Required for LLM interface
- **Kaos**: Required for tool I/O operations

## Success Criteria

- [ ] runTurn function implemented and passing tests
- [ ] Step execution with proper abort handling
- [ ] Tool call batch processing working
- [ ] Retry logic implemented for transient errors
- [ ] Loop hooks interface defined
- [ ] All unit tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Loop correctness | High | Comprehensive test coverage, property-based testing |
| Error propagation | Medium | Clear error types, proper cleanup |
| Tool execution timeout | Medium | Implement timeout per tool, overall turn timeout |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Stateless Loop**: runTurn has no hidden state, all dependencies injected
2. **Error Resilience**: Tool failures do not crash the loop
3. **Abort Support**: Loop respects abort signals for cancellation
4. **Deterministic Execution**: Same inputs produce same behavior

## References

- [Kimi Code Loop Engine](../../kimi-code-analysis/core-components.md#2-loop-system)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [OpenAI: Agent-First Engineering](https://openai.com/index/codex-in-an-agent-first-world/)
