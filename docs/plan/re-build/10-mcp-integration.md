# MCP Integration

**Source**: `packages/agent-core/src/mcp/`

## Purpose

MCP (Model Context Protocol) is an open protocol that allows LLM agents to call external tools exposed by servers. Kimi Code CLI acts as an MCP **client**, connecting to MCP servers that provide additional tools beyond the built-in ones.

MCP tools are indistinguishable from built-in tools from the agent's perspective — they're registered in the ToolManager, subject to the same permission rules, and participate in the same approval flow.

## Architecture

```
McpConnectionManager
│
├── Server "filesystem" ─── StdioMcpClient ─── child_process (npx @mcp/server-filesystem)
├── Server "linear"    ─── HttpMcpClient  ─── HTTP SSE (linear.app/mcp)
├── Server "github"    ─── StdioMcpClient ─── child_process (npx @mcp/github)
│
├── onStatusChange(listener)  → ToolManager.registerMcpServer / unregisterMcpServer
├── connectAll()              → Parallel server startup
├── reconnect(name)           → Reconnect a failed server
└── shutdown()                → Disconnect all servers
```

## Connection Manager

**Source**: `packages/agent-core/src/mcp/connection-manager.ts`

```typescript
class McpConnectionManager {
  constructor(config: McpConnectionManagerConfig);

  /** Connect to all configured servers in parallel */
  async connectAll(): Promise<void>;

  /** Connect/reconnect a specific server */
  async connectServer(name: string): Promise<void>;

  /** Reconnect a failed/needs-auth server */
  async reconnect(name: string): Promise<void>;

  /** Disconnect a specific server */
  async disconnectServer(name: string): Promise<void>;

  /** Disconnect all servers */
  async shutdown(): Promise<void>;

  /** Listen for status changes */
  onStatusChange(listener: McpStatusListener): void;

  /** Get current status of all servers */
  getStatus(): McpServerStatus[];

  /** Get startup metrics */
  getStartupMetrics(): McpStartupMetrics;
}

interface McpConnectionManagerConfig {
  servers: McpServerConfig[];
  enabledTools?: string[];     // If set, only these tools are exposed
  disabledTools?: string[];    // These tools are hidden from the agent
}

interface McpServerStatus {
  name: string;
  status: 'pending' | 'connected' | 'failed' | 'disabled' | 'needs-auth';
  error?: string;
  tools?: string[];            // Tool names provided by this server
}
```

### Server States

```
                  connectServer() called
                         │
                         ▼
                     ┌─────────┐
              ┌──────│ pending │
              │      └─────────┘
              │           │
              │    ┌──────┴──────┐
              │    │             │
              ▼    ▼             ▼
        ┌─────────┐       ┌──────────┐
        │disabled │       │connected │
        └─────────┘       └────┬─────┘
                                │
                    ┌───────────┼───────────┐
                    │           │           │
                    ▼           ▼           ▼
              ┌────────┐ ┌───────────┐ ┌──────────┐
              │ failed │ │needs-auth │ │connected │ (reconnected)
              └────────┘ └───────────┘ └──────────┘
                              │
                     OAuth flow completes
                              │
                              ▼
                         ┌───────────┐
                         │connected  │
                         └───────────┘
```

## MCP Client Interface

**Source**: `packages/agent-core/src/mcp/types.ts`

```typescript
interface MCPClient {
  /** List all tools provided by this server */
  listTools(): Promise<MCPToolDefinition[]>;

  /** Call a tool on this server */
  callTool(
    name: string,
    args: Record<string, unknown>,
    signal?: AbortSignal,
  ): Promise<MCPToolResult>;
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

type MCPToolContent =
  | { type: 'text'; text: string }
  | { type: 'image'; data: string; mimeType: string }
  | { type: 'resource'; resource: MCPResource };
```

## Transport Implementations

### Stdio MCP Client

**Source**: `packages/agent-core/src/mcp/client-stdio.ts`

```typescript
class StdioMcpClient implements MCPClient {
  constructor(name: string, config: StdioServerConfig);
  
  // config: { command, args, env?, cwd? }
  // Spawns: child_process.spawn(command, args, { stdio: ['pipe', 'pipe', 'pipe'] })
}
```

**Communication**: JSON-RPC 2.0 over stdin/stdout:
- Send requests via stdin: `{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}\n`
- Receive responses via stdout: `{"jsonrpc":"2.0","id":1,"result":{"tools":[...]}}\n`
- Stderr from child process is captured for debugging

### HTTP MCP Client

**Source**: `packages/agent-core/src/mcp/client-http.ts`

```typescript
class HttpMcpClient implements MCPClient {
  constructor(name: string, config: HttpServerConfig);
  
  // config: { url, headers?, bearerTokenEnvVar? }
  // Uses @modelcontextprotocol/sdk StreamableHTTPClientTransport
}
```

**Communication**: HTTP SSE (Server-Sent Events):
- GET endpoint for receiving server events
- POST endpoint for sending client requests
- Supports OAuth via `Authorization` header or `bearerTokenEnvVar`

## Tool Naming

**Source**: `packages/agent-core/src/mcp/tool-naming.ts`

MCP tools are qualified with the server name to avoid naming collisions:

```typescript
function qualifyMcpToolName(serverName: string, toolName: string): string {
  // Format: mcp__<server>__<tool>
  // Max 64 characters (truncated with hash if needed)
  const qualified = `mcp__${serverName}__${toolName}`;
  if (qualified.length <= 64) return qualified;
  
  // Truncate with hash for uniqueness
  const hash = shortHash(qualified);
  return `mcp__${serverName.slice(0, 20)}__${hash}`;
}
```

## OAuth for MCP

**Source**: `packages/agent-core/src/mcp/oauth/`

Some HTTP MCP servers require OAuth authentication. The system handles this through:

1. **OAuth Detection**: When an HTTP MCP server returns 401, its status changes to `needs-auth`.

2. **Synthetic Auth Tool**: A synthetic `mcp__<server>__authenticate` tool is created. When the agent calls it:
   - Server's OAuth metadata is discovered
   - Authorization URL is streamed to the user
   - A localhost callback server listens for the redirect
   - Token is stored in `~/.kimi-code/credentials/mcp/<server>.json`

3. **Token Storage**: Atomic file writes using `proper-lockfile` for concurrency safety.

4. **Reconnection**: After authentication succeeds, the server is reconnected automatically.

```typescript
// OAuth service orchestration
class McpOAuthService {
  async startOAuth(serverName: string, metadata: OAuthMetadata): Promise<void>;
  // 1. Start callback server on localhost
  // 2. Open browser with authorization URL
  // 3. Wait for callback (up to 15 minutes)
  // 4. Exchange code for token
  // 5. Store token
  // 6. Reconnect server
}
```

## Config Loading

**Source**: `packages/agent-core/src/mcp/config-loader.ts`

MCP server configuration comes from `mcp.json` files:

```typescript
interface McpJsonConfig {
  mcpServers: Record<string, McpServerConfig>;
}

type McpServerConfig = StdioServerConfig | HttpServerConfig;

interface StdioServerConfig {
  command: string;
  args?: string[];
  env?: Record<string, string>;
  cwd?: string;
}

interface HttpServerConfig {
  url: string;
  headers?: Record<string, string>;
  bearerTokenEnvVar?: string;  // Read bearer token from this env var
}
```

### Config Resolution

Two levels of MCP config, with project-level overriding user-level:

```
User-level:   ~/.kimi-code/mcp.json (or $KIMI_CODE_HOME/mcp.json)
Project-level: .kimi-code/mcp.json  (current working directory)

Resolution: project-level entries override user-level entries with the same name
```

The easiest way to configure MCP is via the TUI's `/mcp-config` command, which provides an interactive editor.

## Tool Registration Flow

```
1. Session initializes McpConnectionManager
2. connectAll() is called:
   - For each server in config, create MCPClient (stdio or HTTP)
   - Call client.listTools() to discover tools
   - On success: status = 'connected'
   - On failure: status = 'failed' (other servers unaffected)
3. For each connected server:
   - Qualify tool names: mcp__server__toolName
   - Call ToolManager.registerMcpServer(serverName, qualifiedTools)
   - Agent now has access to these tools
4. On status change:
   - ToolManager.unregisterMcpServer (on disconnect)
   - ToolManager.registerMcpServer (on reconnect)
```

## Re-implementation Notes

1. **MCP uses JSON-RPC 2.0**: The wire protocol is standard JSON-RPC 2.0. If you're implementing an MCP client, you just need to send/receive JSON messages over stdin or HTTP.

2. **Per-server isolation**: One server failing doesn't affect others. Each server has its own client instance and status tracking.

3. **Tool name qualification is important**: Without the `mcp__server__tool` prefix, two MCP servers could have conflicting tool names (e.g., both have a "create-issue" tool). The qualification prevents collisions.

4. **Stdio vs HTTP**: Stdio servers are simpler (no network, no auth). HTTP servers need OAuth support. For initial port, start with stdio MCP servers.

5. **OAuth is optional**: Many MCP servers don't require authentication. The OAuth flow adds complexity (browser launch, callback server, token storage). Skip for initial port.

6. **Tool lifecycle**: MCP tools are dynamically registered/unregistered as servers connect/disconnect. The ToolManager handles this via `registerMcpServer`/`unregisterMcpServer`.