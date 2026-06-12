# Plan: Implement CLI and TUI Layer

## Status
- **Phase**: Application
- **Priority**: Low
- **Estimated Effort**: 3 weeks
- **Owner**: Core Team

## Objective

Implement the CLI entry point and terminal UI for user interaction.

## Scope

### In Scope
- Implement CLI argument parsing
- Implement interactive TUI
- Implement non-interactive mode
- Add reverse-RPC for approvals
- Add unit tests

### Out of Scope
- Advanced TUI features (future phase)
- Multiple TUI themes (future phase)
- Performance optimization (future phase)

## Implementation Steps

### Week 1: CLI Entry Point

1. **Define CLI Types**
   - Create `src/codeharness/cli/cli_types.h`
   - Define `CLIOptions` for command-line arguments
   - Define `UIMode` (shell, print)
   - Define `CLIConfig` for CLI settings

2. **Implement CLI Parser**
   - Create `src/codeharness/cli/cli_parser.h` and `cli_parser.cpp`
   - Implement argument parsing with CLI11
   - Implement option validation
   - Implement help generation

3. **Implement Main Entry**
   - Create `src/codeharness/cli/main.cpp`
   - Implement process initialization
   - Implement crash handlers
   - Implement CLI routing

4. **Create Unit Tests**
   - Create `tests/cli/cli_tests.cpp`
   - Test argument parsing
   - Test option validation
   - Test help generation

### Week 2: Non-Interactive Mode

5. **Implement runPrompt**
   - Create `src/codeharness/cli/run_prompt.h` and `run_prompt.cpp`
   - Implement single-prompt execution
   - Implement response streaming
   - Implement output formatting

6. **Implement Output Formats**
   - Implement text format
   - Implement stream-json format
   - Implement error output

### Week 3: Interactive TUI

7. **Implement TUI Framework**
   - Create `src/codeharness/tui/tui_app.h` and `tui_app.cpp`
   - Implement terminal initialization
   - Implement event loop
   - Implement component rendering

8. **Implement TUI Components**
   - Create `src/codeharness/tui/components/`
   - Implement transcript component
   - Implement activity component
   - Implement editor component
   - Implement footer component

9. **Implement Reverse-RPC**
   - Create `src/codeharness/tui/reverse_rpc.h` and `reverse_rpc.cpp`
   - Implement approval panel
   - Implement question dialog
   - Implement event rendering

10. **Integration Testing**
    - Create integration tests for CLI
    - Create integration tests for TUI
    - Test complete user flow
    - Test error scenarios

11. **Documentation**
    - Update architecture docs to include CLI/TUI
    - Add CLI reference
    - Add TUI user guide

## Dependencies

- **Agent**: Required for agent integration
- **SDK**: Required for session management

## Success Criteria

- [ ] CLI argument parsing working
- [ ] Non-interactive mode working
- [ ] Interactive TUI working
- [ ] Reverse-RPC working
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Terminal compatibility | Medium | Test on multiple terminals, use standard libraries |
| TUI complexity | High | Start simple, add features incrementally |
| Event handling | Medium | Comprehensive error handling, proper cleanup |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |

## Architecture Invariants

This plan enforces the following invariants:

1. **Platform Independence**: CLI/TUI works on Windows and Linux
2. **Clean Shutdown**: TUI exits cleanly
3. **Error Display**: Errors are displayed clearly
4. **Responsive UI**: TUI remains responsive during operations

## References

- [Kimi Code CLI/TUI](../../kimi-code-analysis/core-components.md#15-cli-and-tui-layer)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
