# Provider/API C++20 重写方案

Provider 模块负责把内部统一消息模型转换成不同模型服务的 wire format，并把 streaming 响应转换回内部事件。

上游关键文件：

- `docs/OpenHarness/src/openharness/api/client.py`
- `docs/OpenHarness/src/openharness/api/openai_client.py`
- `docs/OpenHarness/src/openharness/api/codex_client.py`
- `docs/OpenHarness/src/openharness/api/copilot_client.py`
- `docs/OpenHarness/src/openharness/api/provider.py`
- `docs/OpenHarness/src/openharness/api/usage.py`
- `docs/OpenHarness/src/openharness/auth/*`

## Provider 模块职责

Provider 不负责 agent loop。它只负责：

1. 把内部 `ApiMessageRequest` 转换成 HTTP request。
2. 发起 HTTP 请求。
3. 解析 streaming 响应。
4. 组装 assistant message。
5. 解析 usage、stop reason、tool calls。
6. 把 provider 错误转换成统一错误类型。
7. 执行 retry/backoff。

Engine 不应该知道 OpenAI 的 `tool_calls` 或 Anthropic 的 `tool_use` 细节。

## 统一接口

```cpp
struct ApiMessageRequest {
    std::string model;
    std::vector<ConversationMessage> messages;
    std::string systemPrompt;
    int maxTokens = 4096;
    std::vector<nlohmann::json> tools;
    std::optional<std::string> effort;
};

struct ApiTextDelta {
    std::string text;
};

struct ApiMessageComplete {
    ConversationMessage message;
    std::string stopReason;
    nlohmann::json usage;
};

struct ApiRetryEvent {
    std::string message;
    int attempt = 0;
    double delaySeconds = 0;
};

using ApiStreamEvent = std::variant<ApiTextDelta, ApiMessageComplete, ApiRetryEvent>;

class IStreamingClient {
public:
    virtual ~IStreamingClient() = default;
    virtual void streamMessage(const ApiMessageRequest& request,
                               std::function<void(ApiStreamEvent)> onEvent) = 0;
};
```

## OpenAI-compatible 转换

OpenAI chat completions 的消息格式与内部 block 不同。

内部：

```text
assistant message:
  TextBlock("...")
  ToolUseBlock(id="call_1", name="read_file", input={...})
```

OpenAI wire format：

```json
{
  "role": "assistant",
  "content": "...",
  "tool_calls": [
    {
      "id": "call_1",
      "type": "function",
      "function": {
        "name": "read_file",
        "arguments": "{...json string...}"
      }
    }
  ]
}
```

工具结果在 OpenAI 中是单独的 `tool` role message：

```json
{
  "role": "tool",
  "tool_call_id": "call_1",
  "content": "file content"
}
```

因此 OpenAI client 需要做：

- 内部 user text/image blocks -> OpenAI content parts。
- 内部 assistant tool use blocks -> `tool_calls`。
- 内部 tool result blocks -> `role=tool` messages。
- streaming delta 中的 `tool_calls[].function.arguments` 分片累积。

## Anthropic-compatible 转换

Anthropic Messages API 更接近内部模型：

- `text` block。
- `tool_use` block。
- `tool_result` block。

因此 Anthropic client 的转换会简单一些。

但仍要处理：

- system prompt 单独字段。
- tool schema 格式。
- streaming event 类型。
- usage 统计。
- stop reason。

## Streaming 解析

### SSE 基础

很多 provider 使用 Server-Sent Events，响应类似：

```text
event: message_start
data: {...}

event: content_block_delta
data: {...}

event: message_stop
data: {...}
```

OpenAI-compatible 通常是：

```text
data: {"choices":[{"delta":{"content":"hi"}}]}

data: [DONE]
```

C++ 中需要一个 SSE parser：

```cpp
class SseParser {
public:
    std::vector<SseEvent> feed(std::string_view bytes);
};
```

它要能处理半包，例如一次网络 read 只读到半行。

### Tool arguments 累积

OpenAI streaming 里 function arguments 可能这样到达：

```text
{"path":"src
/main.cpp"}
```

你不能每个 delta 都直接 parse JSON。必须按 tool call id 或 index 拼接完整字符串，等 finish 后再 parse。

## HTTP client 选择

按当前项目约束，网络库统一使用 standalone Asio。Provider、MCP HTTP、后续 gateway 本地网络能力都应复用同一套 `network/` 基础设施。

建议模块：

```text
src/codeharness/network/
  io_context.hpp
  tcp_stream.hpp
  tls_stream.hpp
  url.hpp
  http_request.hpp
  http_response.hpp
  http_client.hpp
  sse_parser.hpp
  compression.hpp
```

依赖：

- `asio`：TCP、timer、异步 I/O。
- `openssl`：HTTPS/TLS，通过 `asio::ssl` 使用。
- `ada`：URL parser。
- `zlib` 和 `brotli`：HTTP response 解压。
- 项目内实现 HTTP framing、SSE parser 和 provider-specific streaming 状态机，避免引入未被 awesome-cpp 收录的 HTTP parser。

第一版可以先做阻塞式封装，但底层仍使用 Asio：

```cpp
class HttpClient {
public:
    HttpResponse postJson(const Url& url,
                          const std::map<std::string, std::string>& headers,
                          std::string body,
                          std::function<void(std::string_view)> onChunk);
};
```

后续再把接口改成 coroutine 或 callback 驱动。

## 错误模型

```cpp
enum class ApiErrorKind {
    Authentication,
    RateLimit,
    Network,
    Timeout,
    ContextTooLong,
    InvalidRequest,
    ProviderFormat,
    Unknown
};

struct ApiError {
    ApiErrorKind kind;
    std::string message;
    int httpStatus = 0;
    std::optional<int> retryAfterSeconds;
};
```

Provider client 应该把 HTTP 状态码转换为统一错误：

| HTTP 状态 | 建议错误 |
| --- | --- |
| 401/403 | `Authentication` |
| 408/504 | `Timeout` |
| 429 | `RateLimit` |
| 400 且包含 context length | `ContextTooLong` |
| 5xx | `Network` 或 `ProviderFormat` |

## Retry/backoff

建议只对可恢复错误重试：

- 网络临时失败。
- 429。
- 5xx。

不要重试：

- 401/403。
- schema 错误。
- 无效模型名。
- 工具结果缺失导致的请求错误。

退避策略：

```text
delay = min(base * 2^attempt + jitter, maxDelay)
```

重试时 emit `ApiRetryEvent`，这样 UI 能显示“正在重试”。

## Auth 和 Profile

上游支持多种 auth source：

- Anthropic API key。
- OpenAI-compatible API key。
- Claude subscription。
- Codex subscription。
- GitHub Copilot OAuth。
- Moonshot、DashScope、Gemini、MiniMax 等兼容端点。

C++ 第一版建议只做：

- API key 环境变量。
- settings.json 中 profile。
- OpenAI-compatible base_url/model/api_key。
- Anthropic-compatible base_url/model/api_key。

后续再做 Copilot/Codex subscription。

配置结构建议：

```cpp
struct ProviderProfile {
    std::string name;
    std::string label;
    std::string apiFormat;   // openai | anthropic
    std::string model;
    std::string baseUrl;
    std::string authSource;  // env | profile_api_key | external
};

struct ResolvedAuth {
    std::string apiKey;
    std::map<std::string, std::string> extraHeaders;
};
```

## 安全注意事项

- 不要把 API key 写进日志。
- 错误消息里要清理 `Authorization` header。
- credentials 文件权限应尽量限制，Windows 下至少不要放到项目目录。
- provider base URL 要做基本校验，避免误把本地敏感服务当模型 endpoint。
- web proxy 要显式配置，不要默认继承所有环境变量。

## CMake/vcpkg 集成建议

初版依赖可以这样规划：

```json
{
  "dependencies": [
    "nlohmann-json",
    "cli11",
    "spdlog",
    "asio",
    "openssl",
    "ada",
    "zlib",
    "brotli",
    "re2"
  ]
}
```

然后：

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

target_link_libraries(codeharness_core PUBLIC
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    spdlog::spdlog
)
```

说明：外部依赖从 awesome-cpp 收录项目中挑选。`re2` 用于 grep/search 正则能力；网络 streaming 不使用 libcurl/cpp-httplib/Beast，统一走 Asio。

## 测试建议

Provider 不能只靠真实 API 测试。建议写三类测试：

1. JSON request snapshot：内部消息转 OpenAI/Anthropic wire format 是否正确。
2. SSE parser：半包、多个 event、`[DONE]`、tool arguments 分片。
3. Fake HTTP server：返回 scripted streaming，验证 client 产出正确 `ApiStreamEvent`。
