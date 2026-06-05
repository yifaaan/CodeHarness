# mcp/ — Model Context Protocol 模块

## 设计目标

实现 [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) 客户端，通过 JSON-RPC 2.0 与外部工具服务器通信。让 CodeHarness 能够使用 MCP 服务器提供的工具和资源。

## 架构

```
McpClientSession                       ← JSON-RPC 会话管理
  ├─ McpTransport (抽象接口)            ← 传输层
  │    ├─ McpStdioTransport            ← 子进程 stdin/stdout
  │    └─ (预留) McpHttpTransport      ← HTTP 传输
  └─ JSON-RPC 消息层
       ├─ make_mcp_request()
       ├─ make_mcp_notification()
       └─ parse_mcp_response()

McpToolAdapter                         ← 适配层
  └─ 将 MCP 工具包装为 Tool 接口        ← 让 Engine 通过 ToolRegistry 使用
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `McpStdioServerConfig` / `McpHttpServerConfig` | MCP 服务器配置 |
| `McpToolInfo` / `McpResourceInfo` | 工具/资源元信息 |
| `McpTransport` | 传输层抽象接口 |
| `McpStdioTransport` | 基于 `reproc` 的子进程传输实现 |
| `McpClientSession` | JSON-RPC 会话：`initialize()`、`list_tools()`、`call_tool()`、`read_resource()` |
| `McpToolAdapter` | 将 MCP 工具包装为标准 `Tool`，注册到 `ToolRegistry` |
| `McpToolExecutor` | MCP 工具执行接口 |

## 设计要点

- 分层设计：传输层与协议层分离，可扩展支持 HTTP 等传输方式
- `McpToolAdapter` 是关键适配器，它将 MCP 的工具模型映射到 CodeHarness 的 `Tool` 抽象
- 协议基于 JSON-RPC 2.0，通过 stdin/stdout 传 JSON Lines

## 初学者指南

- MCP 是让 CodeHarness 使用外部工具（如数据库查询、API 调用）的标准协议
- 区分两个概念：MCP 是"工具服务器协议"，不是 LLM 提供商
- 典型路径：`ToolRegistry` → `McpToolAdapter` → `McpClientSession::call_tool()` → `McpStdioTransport` → 外部进程
