# Plan: Implement Kaos Abstraction Layer

## Status
- **Phase**: Foundation
- **Priority**: High
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the Kaos abstraction layer to provide a unified interface for filesystem and process operations, enabling tool portability and testability.

## Scope

### In Scope
- Define unified Kaos interface
- Implement LocalKaos for local filesystem/process operations
- Migrate existing tools to use Kaos
- Add unit tests for Kaos implementations
- Update documentation

### Out of Scope
- SSHKaos implementation (future phase)
- Performance optimization (future phase)
- Remote execution support (future phase)

## Implementation Steps

### Week 1: Interface Design and LocalKaos

1. **Define Kaos Interface**
   - Create `src/codeharness/kaos/kaos.h`
   - Define file system operations: `readText`, `writeText`, `glob`, `stat`, `iterdir`
   - Define process execution: `exec`
   - Define path operations: `resolve`, `relative`

2. **Implement LocalKaos**
   - Create `src/codeharness/kaos/local_kaos.h` and `local_kaos.cpp`
   - Implement all interface methods using standard C++ filesystem
   - Add error handling and Result<T> returns

3. **Create Unit Tests**
   - Create `tests/kaos/local_kaos_tests.cpp`
   - Test all file system operations
   - Test process execution
   - Test error cases

### Week 2: Tool Migration and Integration

4. **Migrate Existing Tools**
   - Update `read_file` tool to use Kaos
   - Update `write_file` tool to use Kaos
   - Update `edit_file` tool to use Kaos
   - Update `glob` tool to use Kaos
   - Update `grep` tool to use Kaos

5. **Update Tool Registry**
   - Modify `ToolRegistry` to accept Kaos dependency
   - Update tool constructors to accept Kaos reference
   - Ensure tools use Kaos for all I/O operations

6. **Integration Testing**
   - Create integration tests for tools with Kaos
   - Test complete tool execution flows
   - Test error handling and recovery

7. **Documentation**
   - Update architecture docs to include Kaos
   - Add API reference for Kaos interface
   - Update tool documentation

## Dependencies

- **None**: This is the first module in the dependency chain

## Success Criteria

- [ ] Kaos interface defined with all required methods
- [ ] LocalKaos implemented and passing all unit tests
- [ ] All file system tools migrated to use Kaos
- [ ] Tool execution tests passing with Kaos
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Interface design too complex | Medium | Start minimal, iterate based on tool needs |
| Performance overhead | Low | Use inline functions, profile later |
| Migration breaks existing tests | Medium | Run tests after each tool migration |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Dependency Direction**: Kaos is at the bottom of the dependency chain
2. **Error Handling**: All operations return Result<T>
3. **RAII**: Resource management through RAII
4. **Testability**: All operations are testable with mocks

## References

- [Kimi Code Kaos Interface](../../kimi-code-analysis/core-components.md#10-kaos-abstraction)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [OpenAI: Agent-First Engineering](https://openai.com/index/codex-in-an-agent-first-world/)
