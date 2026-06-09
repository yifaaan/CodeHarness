# Provider/API C++20 实现参考

Provider 模块的 C++20 实现已完成，代码见 `src/codeharness/provider/`（24 个文件，最大的模块）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| **统一 Provider 接口** | `provider/provider.h` — `Provider` 抽象基类，`stream_message(Request, on_event)` callback 驱动 |
| **EchoProvider** | `provider/echo_provider.h/.cpp` — 测试用 mock，返回固定响应 |
| **OpenAIProvider** | `provider/openai_provider.h/.cpp` — Chat Completions API，tool_calls 参数分片累积 |
| **AnthropicProvider** | `provider/anthropic_provider.h/.cpp` — Messages API，更接近内部 block 模型 |
| **消息格式转换** | OpenAI：内部 tool_use → function calling；Anthropic：直接 block 映射 |
| **SSE 解析** | `network/sse_parser.h/.cpp` — 处理半包、多个 event、`[DONE]`、tool arguments 分片 |
| **HTTP 客户端** | `network/http_client.h/.cpp` — 基于 Asio + OpenSSL 的 HTTP POST，streaming chunk callback |
| **Auth** | API key 从环境变量（`OPENAI_API_KEY`、`ANTHROPIC_API_KEY`）或 credentials.json 读取 |
| **Retry/backoff** | 429/5xx/网络临时失败 → 指数退避 + jitter；401/400 不重试 |
| **错误模型** | `ApiErrorKind` — Authentication、RateLimit、Network、Timeout、ContextTooLong、InvalidRequest、ProviderFormat、Unknown |
| **CostTracker** | `provider/cost_tracker.h/.cpp` — usage token 统计（选 provider，待 engine 集成）|

## Provider 职责

Provider **不负责 agent loop**，只负责：

1. 内部消息 → HTTP request wire format
2. 发起 HTTP 请求
3. 解析 streaming 响应
4. 组装 assistant message（tool calls + text）
5. 解析 usage、stop reason
6. 错误转换 + retry/backoff

## 统一接口

```
Provider (抽象基类)
  Provider::stream_message(Request, onEvent)
    → Request: model, messages, systemPrompt, maxTokens, tools
    → Event: ApiTextDelta | ApiMessageComplete | ApiRetryEvent

  Provider 子类:
    OpenAIProvider    — Chat Completions, tool_calls
    AnthropicProvider — Messages API, tool_use blocks
    EchoProvider      — 测试用 mock
```

## Wire format 差异

| 方面 | OpenAI | Anthropic |
| --- | --- | --- |
| 工具调用 | `tool_calls` → function name + arguments (JSON string) | `tool_use` → name + input (JSON object) |
| 工具结果 | `role: tool` 独立消息 | `role: user` + `tool_result` block |
| System prompt | `system` role message | 单独 `system` 字段 |
| Streaming | SSE `data:` 行，`tool_calls` delta 分片 | 结构化 `content_block_delta` 事件 |

## 底层网络

网络模块在 `src/codeharness/network/`：

- `http_client.h/.cpp` — Asio + OpenSSL，阻塞式封装，streaming chunk callback
- `sse_parser.h/.cpp` — 半包缓冲、多 event 解析、`[DONE]` 终止

统一使用 standalone Asio，不混用 libcurl/cpp-httplib/Beast。

## 测试策略

`tests/provider_tests.cpp`（526 行）覆盖：

1. **JSON request snapshot**：内部消息 → OpenAI/Anthropic wire format 快照对比
2. **SSE parser**：半包、多 event、`[DONE]`、tool arguments 分片
3. **Fake HTTP server**：返回 scripted streaming 验证 `ApiStreamEvent` 输出
4. **Provider 集成**：EchoProvider → Engine 集成测试，验证 tool use → ToolResult 全流程
