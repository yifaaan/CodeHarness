# MCP Integration

Model Context Protocol client.

## One-Liner

MCP client that connects to external tool servers, making their tools available to the agent alongside built-in tools.

## Architecture

```
McpConnectionManager
├── Server "filesystem" ── StdioMcpClient ── child_process
├── Server "linear"    ── HttpMcpClient  ── HTTP SSE
├── Server "github"    ── StdioMcpClient ── child_process
│
├── onStatusChange(listener)
├── connectAll()
├── reconnect(name)
└── shutdown()
```

## MCP Client Interface

```typescript
interface MCPClient {
  listTools(): Promise<MCPToolDefinition[]>;
  callTool(name: string, args: Record<string, unknown>, signal?: AbortSignal): Promise<MCPToolResult>;
}

interface MCPToolDefinition {
  name: string;
  description: string;
  inputSchema: JSONSchema;
}

interface MCPToolResult {
  content: MCPToolContent[];
  isError?: boolean;
}
```

## Server States

```
pending → connected → failed
                ↓
           needs-auth → connected (after OAuth)
```

## Transport Implementations

| Transport | Protocol | Use Case |
|-----------|----------|----------|
| Stdio | JSON-RPC 2.0 over stdin/stdout | Local servers |
| HTTP | SSE over HTTP | Remote services |

## Tool Naming

MCP tools qualified with server name to avoid collisions:
```typescript
// Format: mcp__<server>__<tool>
const qualified = `mcp__${serverName}__${toolName}`;
```

## Config Loading

Two levels with project-level overriding user-level:
```
User-level:   ~/.kimi-code/mcp.json
Project-level: .kimi-code/mcp.json
```

## OAuth Flow

1. Server returns 401 → status = `needs-auth`
2. Synthetic `mcp__<server>__authenticate` tool created
3. Agent calls it → OAuth flow starts
4. Token stored in `~/.kimi-code/credentials/mcp/<server>.json`
5. Server reconnected automatically

## See Also

- [tool-system.md](tool-system.md) — Tool system integration
- [config-schema.md](config-schema.md) — MCP configuration
