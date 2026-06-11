# Model Context Protocol

CodeHarness 可以作为 MCP client 连接外部 MCP server，并把外部工具适配为内部 `Tool` 接口。这样 Engine 不需要区分内置工具和 MCP 工具，统一通过 `ToolRegistry` 调用。

## 集成范围

当前配置结构支持：

- stdio server：启动本地子进程，通过 JSON-RPC 通信。
- HTTP server：连接远端 MCP endpoint。
- 工具列表和资源列表状态展示。
- 将 MCP 工具包装为 `mcp__<server>__<tool>` 形式的工具名。

## 配置

MCP server 写在 `settings.json` 的 `mcp_servers` 数组中。

stdio 示例：

```json
{
  "mcp_servers": [
    {
      "transport": "stdio",
      "name": "filesystem",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "."],
      "env": {
        "NODE_ENV": "production"
      }
    }
  ]
}
```

HTTP 示例：

```json
{
  "mcp_servers": [
    {
      "transport": "http",
      "name": "linear",
      "url": "https://example.com/mcp",
      "headers": {
        "Authorization": "Bearer token"
      }
    }
  ]
}
```

## 工具命名与权限

MCP 工具使用统一命名：

```text
mcp__<server-name>__<tool-name>
```

它们和内置工具一样进入权限评估。可以在 `permission.allowed_tools`、`permission.denied_tools` 或 path/command 规则中使用精确工具名做控制。

## 连接状态

MCP 连接状态包括：

| 状态 | 说明 |
| --- | --- |
| `pending` | 尚未完成连接 |
| `connected` | 已连接 |
| `failed` | 连接失败 |
| `disabled` | 已禁用 |

状态记录包含 server 名称、transport、详情、工具列表和资源列表。

## 安全性

stdio server 会执行本地命令，只应启用可信配置。HTTP server 的 headers 可能包含凭据，建议通过本地私有配置管理，不要提交到仓库。
