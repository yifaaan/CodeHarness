# Plan: Implement Tool System

## Status
- **Phase**: Services
- **Priority**: Medium
- **Estimated Effort**: 3 weeks
- **Owner**: Core Team

## Objective

Implement the tool system including the ExecutableTool interface, ToolManager, and built-in tools for file operations, code search, and shell execution.

## Scope

### In Scope
- Implement ExecutableTool interface
- Implement ToolManager for tool registration
- Implement built-in tools (Read, Write, Edit, Glob, Grep, Bash)
- Add tool validation and execution
- Add unit tests

### Out of Scope
- Tool scheduler for concurrent execution (future phase)
- MCP tools (future phase)
- User-defined tools (future phase)

## Implementation Steps

### Week 1: Tool Interface and Manager

1. **Define Tool Types**
   - Create `src/codeharness/tools/tool_types.h`
   - Define `ExecutableTool` interface
   - Define `ToolInput`, `ToolOutput`, `ToolResult` types
   - Define `ToolInfo` for tool metadata

2. **Implement ToolManager**
   - Create `src/codeharness/tools/tool_manager.h` and `tool_manager.cpp`
   - Implement tool registration
   - Implement tool lookup by name
   - Implement tool execution with validation

3. **Create Unit Tests**
   - Create `tests/tools/tool_manager_tests.cpp`
   - Test tool registration
   - Test tool lookup
   - Test tool execution

### Week 2: File Operation Tools

4. **Implement Read Tool**
   - Create `src/codeharness/tools/read_file.h` and `read_file.cpp`
   - Implement file reading with line numbers
   - Implement offset and limit parameters
   - Implement error handling

5. **Implement Write Tool**
   - Create `src/codeharness/tools/write_file.h` and `write_file.cpp`
   - Implement file writing
   - Implement create/overwrite modes
   - Implement backup creation

6. **Implement Edit Tool**
   - Create `src/codeharness/tools/edit_file.h` and `edit_file.cpp`
   - Implement string replacement
   - Implement undo support
   - Implement conflict detection

### Week 3: Search and Execution Tools

7. **Implement Glob Tool**
   - Create `src/codeharness/tools/glob.h` and `glob.cpp`
   - Implement file pattern matching
   - Implement recursive search
   - Implement result sorting

8. **Implement Grep Tool**
   - Create `src/codeharness/tools/grep.h` and `grep.cpp`
   - Implement regex search
   - Implement file filtering
   - Implement context lines

9. **Implement Bash Tool**
   - Create `src/codeharness/tools/bash.h` and `bash.cpp`
   - Implement shell command execution
   - Implement timeout handling
   - Implement output capture

10. **Integration Testing**
    - Create integration tests for all tools
    - Test complete tool execution flows
    - Test error handling scenarios
    - Test permission integration

11. **Documentation**
    - Update architecture docs to include Tools
    - Add API reference for ExecutableTool
    - Add tool development guide

## Dependencies

- **Kaos**: Required for file I/O operations
- **Permission**: Required for access control

## Success Criteria

- [ ] ExecutableTool interface defined
- [ ] ToolManager implemented and tested
- [ ] All 6 built-in tools implemented
- [ ] Tool validation working correctly
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Tool security | High | Input validation, permission checks, sandboxing |
| Bash execution safety | High | Timeout, output limits, dangerous command detection |
| File operation errors | Medium | Comprehensive error handling, rollback support |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |
| 2026-06-13 | Done | Implemented `ToolManager` + Read/Write/Edit/Glob/Grep/Bash under `src/codeharness/tools/`. ExecutableTool interface kept in `engine/`. Grep uses in-process `re2`. Added `HostProcess::Drain` (reproc poll, single-threaded) for deadlock-free Bash output draining. Wired `TurnInput.host` → `ToolContext`. 167 tests passing. Active-set selection, MCP, and user-defined tools deferred. |

## Architecture Invariants

This plan enforces the following invariants:

1. **Tool Isolation**: Tools do not share state
2. **Error Resilience**: Tool failures do not crash the harness
3. **Permission Check**: All tools check permissions before execution
4. **Atomic Operations**: File operations are atomic when possible

## References

- [Kimi Code Tool System](../../kimi-code-analysis/core-components.md#4-toolmanager)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
