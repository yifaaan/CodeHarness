# Plan: Implement Node SDK

## Status
- **Phase**: Application
- **Priority**: Low
- **Estimated Effort**: 1 week
- **Owner**: Core Team

## Objective

Implement the public SDK for embedding CodeHarness agent in other applications.

## Scope

### In Scope
- Implement SDK public API
- Implement session management
- Implement event subscription
- Add authentication facade
- Add unit tests

### Out of Scope
- Advanced SDK features (future phase)
- SDK distribution (future phase)
- SDK documentation site (future phase)

## Implementation Steps

### Week 1: SDK Implementation

1. **Define SDK Types**
   - Create `src/codeharness/sdk/sdk_types.h`
   - Define `KimiHarness` interface
   - Define `Session` wrapper types
   - Define `AgentAPI` for SDK perspective

2. **Implement KimiHarness**
   - Create `src/codeharness/sdk/kimi_harness.h` and `kimi_harness.cpp`
   - Implement harness construction
   - Implement session creation and resumption
   - Implement harness shutdown

3. **Implement Session Wrapper**
   - Create `src/codeharness/sdk/session_wrapper.h` and `session_wrapper.cpp`
   - Implement session operations
   - Implement event subscription
   - Implement session close

4. **Implement Auth Facade**
   - Create `src/codeharness/sdk/auth_facade.h` and `auth_facade.cpp`
   - Implement OAuth login
   - Implement token management
   - Implement authentication check

5. **Create Unit Tests**
   - Create `tests/sdk/sdk_tests.cpp`
   - Test harness construction
   - Test session operations
   - Test auth facade

6. **Documentation**
   - Update architecture docs to include SDK
   - Add API reference for SDK
   - Add SDK integration guide

## Dependencies

- **Agent**: Required for agent integration
- **Session**: Required for session management

## Success Criteria

- [ ] KimiHarness implemented and tested
- [ ] Session wrapper implemented
- [ ] Auth facade implemented
- [ ] All unit tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| API stability | High | Comprehensive API review, versioning |
| Error handling | Medium | Clear error types, comprehensive documentation |
| Thread safety | Medium | Document thread safety requirements |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **API Stability**: Public API is stable and versioned
2. **Error Handling**: All errors are handled gracefully
3. **Resource Cleanup**: All resources are cleaned up
4. **Thread Safety**: SDK is thread-safe where documented

## References

- [Kimi Code SDK](../../kimi-code-analysis/core-components.md#16-node-sdk)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
