# Plan: Implement MCP Integration

## Status
- **Phase**: Extensions
- **Priority**: Low
- **Estimated Effort**: 2 weeks
- **Owner**: Core Team

## Objective

Implement the Model Context Protocol (MCP) client for connecting to external tool servers.

## Scope

### In Scope
- Implement MCP client interface
- Implement stdio transport
- Implement tool discovery and registration
- Add unit tests

### Out of Scope
- MCP server implementation (future phase)
- HTTP transport and OAuth support (future phase)
- Advanced MCP features (future phase)
- Performance optimization (future phase)

## Implementation Steps

### Week 1: MCP Client Core

1. **Define MCP Types**
   - Create `src/codeharness/mcp/mcp_types.h`
   - Define `McpClient` interface
   - Define `McpTool`, `McpServer` types
   - Define `McpTransport` interface

2. **Implement McpConnectionManager**
   - Create `src/codeharness/mcp/mcp_manager.h` and `mcp_manager.cpp`
   - Implement server connection lifecycle
   - Implement tool discovery
   - Implement tool registration

3. **Implement Stdio Transport**
   - Create `src/codeharness/mcp/stdio_transport.h` and `stdio_transport.cpp`
   - Implement JSON-RPC over stdio
   - Implement process management
   - Implement error handling

4. **Create Unit Tests**
   - Create `tests/mcp/mcp_tests.cpp`
   - Test MCP client
   - Test stdio transport
   - Test tool discovery

### Week 2: HTTP Transport and OAuth

5. **Implement HTTP Transport**
   - Create `src/codeharness/mcp/http_transport.h` and `http_transport.cpp`
   - Implement JSON-RPC over HTTP
   - Implement SSE for streaming
   - Implement authentication

6. **Implement OAuth for MCP**
   - Create `src/codeharness/mcp/mcp_oauth.h` and `mcp_oauth.cpp`
   - Implement OAuth device code flow
   - Implement token storage
   - Implement token refresh

7. **Integration Testing**
   - Create integration tests with mock MCP servers
   - Test complete MCP flow
   - Test error scenarios
   - Test OAuth flow

8. **Documentation**
   - Update architecture docs to include MCP
   - Add API reference for McpClient
   - Add MCP server configuration guide

## Dependencies

- **Agent**: Required for tool integration
- **Config**: Required for server configuration
- **Kaos**: Required for process execution

## Success Criteria

- [ ] MCP client interface defined
- [ ] McpConnectionManager implemented
- [ ] Stdio transport working
- [ ] HTTP transport working
- [ ] OAuth support working
- [ ] All unit and integration tests passing
- [ ] Documentation updated
- [ ] Code review completed
- [ ] All linters passing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Transport reliability | High | Comprehensive error handling, retry logic |
| OAuth complexity | Medium | Use well-tested libraries, add comprehensive logging |
| Server compatibility | Medium | Test with multiple MCP server implementations |

## Progress Log

| Date | Status | Notes |
|------|--------|-------|
| 2026-06-12 | Planned | Initial plan created |
| 2026-06-15 | Partial | **MCP stdio MVP landed.** Added `Source/CodeHarness/Mcp/` with `McpServerConfig`, `McpClient`, `StdioMcpClient`, `McpExecutableTool`, and `McpConnectionManager`. Config now parses/saves `[[mcp.servers]]` plus `[mcp.servers.<name>]`; CLI registers discovered MCP tools into the existing `ToolManager` best-effort. MCP tools use `mcp__<server>__<tool>` names and default to `requiresPermission = true`, so the existing PermissionGate and Hook flow applies. Fake stdio tests cover initialize/list/call, malformed JSON, server exit, registration, and loop permission gating; full suite 362/362 green. **Still TODO:** HTTP transport, OAuth/authenticate flow, richer server compatibility, and user-facing configuration UI. |

## Architecture Invariants

This plan enforces the following invariants:

1. **Transport Abstraction**: MCP does not depend on specific transport
2. **Error Resilience**: MCP failures do not crash the harness
3. **Lazy Connection**: Connections established on demand
4. **Clean Shutdown**: Connections close cleanly

## References

- [Kimi Code MCP Integration](../../kimi-code-analysis/core-components.md#11-mcp-integration)
- [MCP Specification](https://spec.modelcontextprotocol.io/)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
