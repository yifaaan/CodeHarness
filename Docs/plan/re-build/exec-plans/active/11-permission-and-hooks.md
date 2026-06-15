# Plan: Implement Permission and Hooks System

## Status
- **Phase**: Services
- **Priority**: Medium
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the permission system for access control and the hook system for extending agent behavior.

## Scope

### In Scope
- Implement PermissionManager with three modes
- Implement permission rules DSL
- Implement HookEngine with 13 hook events
- Add hook execution and matching
- Add unit tests

### Out of Scope
- Advanced permission policies (future phase)
- Hook chaining (future phase)
- Performance optimization (future phase)

## Implementation Steps

### Week 1: Permission System

1. **Define Permission Types**
   - Create `src/codeharness/permissions/permission_types.h`
   - Define `PermissionMode` (manual, yolo, auto)
   - Define `PermissionRule`, `PermissionResult` types
   - Define `Policy` for permission evaluation

2. **Implement PermissionManager**
   - Create `src/codeharness/permissions/permission_manager.h` and `permission_manager.cpp`
   - Implement rule matching
   - Implement policy evaluation
   - Implement user approval flow

3. **Create Unit Tests**
   - Create `tests/permissions/permission_manager_tests.cpp`
   - Test rule matching
   - Test policy evaluation
   - Test mode switching

### Week 2: Hook System

4. **Define Hook Types**
   - Create `src/codeharness/hooks/hook_types.h`
   - Define `HookEvent` (13 types)
   - Define `Hook`, `HookResult` types
   - Define `HookContext` for hook data

5. **Implement HookEngine**
   - Create `src/codeharness/hooks/hook_engine.h` and `hook_engine.cpp`
   - Implement hook registration
   - Implement hook matching with regex
   - Implement hook execution

6. **Integration Testing**
   - Create integration tests for permission flow
   - Create integration tests for hook execution
   - Test complete permission + hook flow
   - Test error scenarios

7. **Documentation**
   - Update architecture docs to include Permission and Hooks
   - Add API reference for PermissionManager
   - Add API reference for HookEngine
   - Add hook development guide

## Dependencies

- **Agent**: Required for agent integration
- **Tools**: Required for tool permission checks

## Success Criteria

- [ ] PermissionManager implemented with three modes
- [ ] Permission rules DSL working
- [ ] HookEngine implemented with 13 events
- [ ] Hook matching and execution working
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Permission bypass | High | Comprehensive testing, security review |
| Hook performance | Medium | Lazy loading, caching |
| Rule complexity | Medium | Start simple, add complexity incrementally |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |
| 2026-06-15 | Partial | **Permission MVP landed.** `PermissionGate` + `ApprovalCallback` implemented (`Source/CodeHarness/Permission/`); loop now consults the gate between `ResolveExecution` and `Execute`, closing the safety hole (TD-003 core). Modes: Manual (callback) + Yolo (allow-all); Auto falls back to Manual. Two new loop events (`PermissionRequestedEvent`/`PermissionDeniedEvent`) round-trip through Records. Agent wires it via `SetPermissionMode`/`SetApprovalCallback`. 12 new tests pass (7 unit + 5 loop). **Still TODO in this plan:** PermissionManager rules DSL + `PermissionRule`/`Policy`, true Auto mode, audit logging, and the full HookEngine (13 events). |

## Architecture Invariants

This plan enforces the following invariants:

1. **Security First**: Permission checks cannot be bypassed
2. **Fail Open**: Hook failures do not block agent
3. **Deterministic Rules**: Same inputs produce same results
4. **Audit Trail**: Permission decisions are logged

## References

- [Kimi Code Permission and Hooks](../../kimi-code-analysis/core-components.md#5-permissionmanager)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
