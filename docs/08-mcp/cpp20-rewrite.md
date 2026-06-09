# MCP C++20 实现参考

MCP 模块的 C++20 实现已完成（stdio transport），代码见 `src/codeharness/mcp/`（13 个文件）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| MCP 配置类型 | `mcp/types.h` — `McpStdioServerConfig`、`McpHttpServerConfig`、`McpServerConfig` variant、`McpToolInfo`、`McpResourceInfo`、`McpConnectionStatus`、`McpToolCallResult` |
| JSON-RPC 2.0 | `mcp/json_rpc.h/.cpp` — 请求/响应/通知构建和解析，自增 id、pending 处理 |
| Transport 抽象 | `mcp/transport.h` — `IMcpTransport` 接口，`McpTransport` variant wrapper |
| StdioTransport | `mcp/stdio_transport.h/.cpp` — 基于 `reproc` 子进程 + stdin/stdout JSON Lines |
| ClientSession | `mcp/client_session.h/.cpp` — initialize → initialized → tools/list → callTool，`McpClientSession` 同步 API |
| ToolAdapter | `mcp/tool_adapter.h/.cpp` — MCP 工具包装为 CodeHarness `Tool`，命名 `mcp__{server}__{tool}` |

## MCP 的作用

MCP server 提供 tools + resources。启动时连接 MCP server，读取工具列表，包装为普通工具。Engine 不区分内置工具和 MCP 工具。

## 初始化流程

```
start transport
→ initialize (JSON-RPC)
→ initialized notification
→ tools/list
→ resources/list (可选，method not found 不视为失败)
```

## Studio Transport（已实现）

- 启动 server 子进程（reproc）
- 写 JSON line 到 stdin
- 从 stdout 读 JSON line（line buffer 处理半行）
- stderr 记录到日志
- 进程退出时标记连接失败

## HTTP Transport

HTTP transport 暂不在当前 C++ 实现范围内。如需添加，要点：

- streamable HTTP + session 管理
- reconnect 处理
- headers 可能包含 auth token，日志需隐藏
- 统一基于 standalone Asio

## Tool Adapter 命名规则

```
mcp__{server_name}__{tool_name}
```

非法字符清洗，保证工具名仅包含 API 支持的字符。

## 错误处理

- Server 启动失败 → 标记 failed，不阻止 runtime 启动
- initialize 超时 / JSON 解析失败 / tool 不存在 → 工具返回 `is_error=true`
- Server 断开 → adapter 返回错误 tool result

## 测试

`tests/mcp_tests.cpp` 覆盖：stdio transport 连接、initialize、tools/list、call_tool、fake MCP server（`tests/fake_mcp_server.cpp`）、server 退出时状态变化、invalid JSON 错误。
