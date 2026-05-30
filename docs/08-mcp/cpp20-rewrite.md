# MCP C++20 重写方案

MCP 是 Model Context Protocol，用来把外部工具服务器接入 agent harness。

上游关键文件：

- `docs/OpenHarness/src/openharness/mcp/types.py`
- `docs/OpenHarness/src/openharness/mcp/config.py`
- `docs/OpenHarness/src/openharness/mcp/client.py`
- `docs/OpenHarness/src/openharness/tools/mcp_tool.py`
- `docs/OpenHarness/src/openharness/tools/list_mcp_resources_tool.py`
- `docs/OpenHarness/src/openharness/tools/read_mcp_resource_tool.py`

## MCP 的作用

MCP server 可以提供：

- tools：模型可调用的外部函数。
- resources：可读取的外部资源。
- prompts：可复用 prompt 模板。

OpenHarness 启动时连接 MCP server，读取工具列表，并把它们包装成普通 OpenHarness 工具。

模型看到的是：

```text
mcp__github__create_issue
mcp__filesystem__read_file
```

Engine 不需要知道这些工具来自 MCP。

## Transport 类型

上游配置支持：

| 类型 | 说明 |
| --- | --- |
| `stdio` | 启动本地子进程，通过 stdin/stdout 传 JSON-RPC |
| `http` | 连接 streamable HTTP MCP server |
| `ws` | 配置类型存在，但上游当前支持有限 |

C++ 第一版建议先做 stdio，第二版做 HTTP。

## 配置结构

```cpp
struct McpStdioServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::optional<std::filesystem::path> cwd;
};

struct McpHttpServerConfig {
    std::string name;
    std::string url;
    std::map<std::string, std::string> headers;
};

using McpServerConfig = std::variant<McpStdioServerConfig, McpHttpServerConfig>;
```

## JSON-RPC 基础

MCP 消息基于 JSON-RPC 2.0。

请求：

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

响应：

```json
{"jsonrpc":"2.0","id":1,"result":{}}
```

通知没有 id：

```json
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

C++ client 必须维护自增 id 和 pending request map。

## Transport 抽象

```cpp
class IMcpTransport {
public:
    virtual ~IMcpTransport() = default;
    virtual void start() = 0;
    virtual void send(const nlohmann::json& message) = 0;
    virtual nlohmann::json read() = 0;
    virtual void close() = 0;
};
```

同步版足够初版。后续可以改成异步：

```cpp
virtual Task<void> sendAsync(nlohmann::json message) = 0;
virtual Task<nlohmann::json> readAsync() = 0;
```

## StdioTransport

职责：

- 启动 server 子进程。
- 写 JSON line 到 stdin。
- 从 stdout 读 JSON line。
- stderr 记录到日志。
- 进程退出时关闭 session。

注意：

- JSON-RPC message 以换行分隔。
- stdout 可能有半行，需要 line buffer。
- server 可能把日志误写 stdout，解析失败要给出清晰错误。

## HttpTransport

HTTP MCP 比 stdio 复杂。建议第二阶段实现。

要点：

- 支持 streamable HTTP。
- 维护 session。
- 处理 reconnect。
- headers 可能包含 auth token，日志要隐藏。

按当前项目约束，HTTP transport 也统一基于 standalone Asio。建议在 `network/` 模块封装一个 Asio + OpenSSL 的 HTTP/SSE client，MCP HTTP 和 provider streaming 共用它。

## McpClientSession

```cpp
class McpClientSession {
public:
    explicit McpClientSession(std::unique_ptr<IMcpTransport> transport);

    void initialize();
    std::vector<McpToolInfo> listTools();
    std::vector<McpResourceInfo> listResources();
    nlohmann::json callTool(std::string_view name, nlohmann::json arguments);
    nlohmann::json readResource(std::string_view uri);

private:
    nlohmann::json request(std::string method, nlohmann::json params);
};
```

初始化流程：

```text
start transport
-> initialize request
-> initialized notification
-> tools/list
-> resources/list optional
```

如果 `resources/list` 返回 method not found，不应认为 server 连接失败。

## McpClientManager

```cpp
class McpClientManager {
public:
    void connectAll();
    void close();
    void reconnectAll();

    std::vector<McpConnectionStatus> statuses() const;
    std::vector<McpToolInfo> listTools() const;
    std::vector<McpResourceInfo> listResources() const;

    nlohmann::json callTool(std::string_view server,
                            std::string_view tool,
                            nlohmann::json arguments);
};
```

连接失败不应该让整个 runtime 启动失败，除非用户设置 required。默认应标记 server failed，并继续运行。

## MCP Tool Adapter

MCP 工具包装为 `ITool`：

```cpp
class McpToolAdapter : public ITool {
public:
    McpToolAdapter(McpClientManager& manager,
                   std::string serverName,
                   McpToolInfo toolInfo);

    std::string_view name() const override;
    std::string_view description() const override;
    nlohmann::json inputSchema() const override;
    ToolResult execute(const nlohmann::json& args,
                       const ToolExecutionContext& ctx) override;
};
```

命名规则：

```text
mcp__{server_name}__{tool_name}
```

要清洗非法字符，保证工具名只包含 API 支持的字符。

## Dynamic schema

MCP server 会返回 JSON Schema。C++ 不需要把它转成 struct，adapter 可以直接：

- 把 schema 暴露给模型。
- 执行时把 JSON arguments 原样发给 MCP server。
- 可选做 JSON Schema validator。

## 错误处理

MCP 错误要变成 tool result，而不是崩溃：

```text
MCP server github is disconnected
MCP tool create_issue failed: ...
```

常见错误：

- server 启动失败。
- initialize 超时。
- JSON 解析失败。
- method not found。
- tool 不存在。
- server 断开。

## 测试建议

先写一个 fake MCP server：

- 读 stdin JSON line。
- 响应 initialize。
- 响应 tools/list。
- 响应 tools/call。

测试：

- stdio 连接成功。
- tools/list 注册成 tool。
- call_tool 返回结果。
- server 退出时 status 变 failed。
- method not found resources/list 不导致连接失败。
- invalid JSON 有清晰错误。
