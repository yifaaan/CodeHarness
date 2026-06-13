# Plan: Implement llm LLM Provider Layer

## Status
- **Phase**: Foundation
- **Priority**: High
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the llm abstraction layer to provide a unified interface for LLM providers, enabling seamless switching between different AI models and streaming responses.

## Scope

### In Scope
- Define ChatProvider interface
- Implement streaming response handling
- Implement message type normalization
- Add provider implementations (OpenAI, Anthropic)
- Add unit tests

### Out of Scope
- Google GenAI provider (future phase)
- Kimi provider (future phase)
- Video input support (future phase)

## Implementation Steps

### Week 1: Interface Design and Core Types

1. **Define ChatProvider Interface**
   - Create `src/codeharness/llm/chat_provider.h`
   - Define `ChatProvider` abstract class
   - Define `Message`, `ContentPart`, `ToolCall`, `ToolResult` types
   - Define `StreamResponse` async iterator

2. **Implement Message Normalization**
   - Create `src/codeharness/llm/message.h` and `message.cpp`
   - Implement message conversion from provider-specific formats
   - Implement content part normalization
   - Implement tool call/result normalization

3. **Create Unit Tests**
   - Create `tests/llm/message_tests.cpp`
   - Test message creation and manipulation
   - Test content part types
   - Test tool call/result types

### Week 2: Provider Implementations

4. **Implement OpenAI Provider**
   - Create `src/codeharness/llm/openai_provider.h` and `openai_provider.cpp`
   - Implement streaming response handling
   - Implement error handling and retry logic
   - Implement token usage tracking

5. **Implement Anthropic Provider**
   - Create `src/codeharness/llm/anthropic_provider.h` and `anthropic_provider.cpp`
   - Implement streaming response handling
   - Implement thinking/extended thinking support
   - Implement error handling

6. **Integration Testing**
   - Create integration tests with mock servers
   - Test streaming response handling
   - Test error scenarios (rate limits, network errors)
   - Test provider switching

7. **Documentation**
   - Update architecture docs to include llm
   - Add API reference for ChatProvider
   - Add provider configuration guide

## Dependencies

- **Config**: Required for provider configuration
- **External**: HTTP client library (libcurl or similar)

## Success Criteria

- [ ] ChatProvider interface defined with all required methods
- [ ] Message types defined and normalized
- [ ] OpenAI provider implemented and passing tests
- [ ] Anthropic provider implemented and passing tests
- [ ] Streaming response handling working
- [ ] Error handling and retry logic implemented
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Streaming complexity | High | Use well-tested async patterns, add comprehensive logging |
| Provider API changes | Medium | Abstract provider details, make updates easy |
| Rate limiting | Medium | Implement exponential backoff, respect retry headers |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Provider Abstraction**: Engine does not depend on specific provider formats
2. **Unified Message Model**: All providers convert to same internal model
3. **Streaming First**: All responses are streamed by default
4. **Error Resilience**: Provider failures do not crash the harness

## References

- [Kimi Code llm Interface](../../kimi-code-analysis/core-components.md#13-provider-system)
- [OpenAI API Documentation](https://platform.openai.com/docs)
- [Anthropic API Documentation](https://docs.anthropic.com/)
