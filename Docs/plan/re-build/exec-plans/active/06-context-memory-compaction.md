# Plan: Implement Context Memory and Compaction

## Status
- **Phase**: Agent Core
- **Priority**: Medium
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the context memory system to manage conversation history, token counting, and context compaction for long conversations.

## Scope

### In Scope
- Implement ContextMemory class
- Implement message storage and retrieval
- Implement token counting
- Implement context compaction (LLM-based summarization)
- Add unit tests

### Out of Scope
- Advanced compaction strategies (future phase)
- Context optimization (future phase)
- Context persistence (future phase)

## Implementation Steps

### Week 1: Context Memory Core

1. **Define Context Types**
   - Create `src/codeharness/engine/context_types.h`
   - Define `ContextMessage`, `PromptOrigin` types
   - Define `TokenUsage`, `CompactionResult` types
   - Define `ContextConfig` for compaction settings

2. **Implement ContextMemory Class**
   - Create `src/codeharness/engine/context.h` and `context.cpp`
   - Implement message storage (append, clear)
   - Implement token counting
   - Implement message projection for LLM

3. **Create Unit Tests**
   - Create `tests/engine/context_tests.cpp`
   - Test message append and retrieval
   - Test token counting
   - Test context clear

### Week 2: Context Compaction

4. **Implement Compaction**
   - Implement `FullCompaction` class
   - Implement LLM-based summarization
   - Implement message replacement
   - Implement compaction triggers

5. **Implement Injection Manager**
   - Create `src/codeharness/engine/injection.h` and `injection.cpp`
   - Implement plan mode injection
   - Implement permission mode injection
   - Implement dynamic context updates

6. **Integration Testing**
   - Create integration tests with mock LLM
   - Test complete compaction flow
   - Test long conversation handling
   - Test compaction error recovery

7. **Documentation**
   - Update architecture docs to include Context
   - Add API reference for ContextMemory
   - Add compaction configuration guide

## Dependencies

- **llm**: Required for LLM-based summarization
- **Agent**: Required for agent context

## Success Criteria

- [ ] ContextMemory class implemented and tested
- [ ] Token counting working correctly
- [ ] Context compaction implemented
- [ ] Injection manager implemented
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Compaction quality | High | Use well-tested summarization prompts, add quality metrics |
| Token counting accuracy | Medium | Use provider-specific tokenizers when available |
| Performance with large contexts | Medium | Implement lazy loading, batch processing |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Token Budget**: Context respects token limits
2. **Compaction Safety**: Compaction does not lose critical information
3. **Message Order**: Messages maintain chronological order
4. **Lazy Evaluation**: Token counting is lazy when possible

## References

- [Kimi Code Context Memory](../../kimi-code-analysis/core-components.md#6-contextmemory)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
