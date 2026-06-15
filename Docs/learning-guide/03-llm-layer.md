# 第3章：LLM 层深度剖析

> LLM 层统一了多个 Provider 的接口，让上层代码不关心具体 API 差异。

## 1. 为什么需要 LLM 抽象？

### 1.1 Provider 差异

不同 LLM Provider 的 API 各不相同：

| 特性 | OpenAI | Anthropic | Google GenAI |
|------|--------|-----------|--------------|
| 认证 | `Authorization: Bearer` | `x-api-key` + `anthropic-version` | `Authorization: Bearer` |
| 工具调用 | `tool_calls` 数组 | `content` 中的 `tool_use` | `functionCall` |
| 流式格式 | SSE（Server-Sent Events） | SSE | 分块 JSON |
| 思考模式 | `reasoning_effort` | `thinking` block | `thinkingConfig` |
| 结束原因 | `finish_reason` 字段 | `stop_reason` 字段 | `finishReason` 字段 |

### 1.2 统一接口的价值

**抽象层的作用**：
```
Tool 层 / Agent 层
        ↓
   ChatProvider（统一接口）
        ↓
┌───────────────┬───────────────┬───────────────┐
│ OpenAiProvider│AnthropicProvider│GoogleProvider│
└───────────────┴───────────────┴───────────────┘
        ↓               ↓               ↓
   OpenAI API      Anthropic API   Google API
```

**好处**：
1. **切换 Provider**：改配置即可，代码不变
2. **测试友好**：Mock ChatProvider，无需真实 LLM
3. **功能统一**：流式响应、工具调用、思考模式统一处理

## 2. ChatProvider 接口详解

### 2.1 接口定义

**位置**：`Source/CodeHarness/Llm/ChatProvider.h`

```cpp
class ChatProvider
{
public:
    virtual ~ChatProvider() = default;

    // Provider 名称（如 "openai"、"anthropic"）
    virtual std::string Name() const = 0;
    
    // 模型名称（如 "gpt-4"、"claude-sonnet-4"）
    virtual std::string ModelName() const = 0;
    
    // 思考模式（如果模型支持）
    virtual std::optional<ThinkingEffort> ThinkingEffortLevel() const = 0;

    // 核心方法：生成响应
    // 参数：
    //   systemPrompt - 系统提示词
    //   tools - 可用工具列表
    //   history - 对话历史
    //   callbacks - 流式回调
    //   stopToken - 取消令牌
    // 返回：成功或错误状态
    virtual absl::Status Generate(
        std::string_view systemPrompt,
        std::span<const Tool> tools,
        std::span<const Message> history,
        const StreamCallbacks& callbacks,
        std::stop_token stopToken = {}
    ) = 0;
};
```

### 2.2 Generate 方法流程

```
Generate(systemPrompt, tools, history, callbacks)
        ↓
构建 HTTP 请求
        ↓
发送到 Provider API
        ↓
接收 SSE 流
        ↓
解析响应 → 调用回调
        ↓
完成 → 返回 Status
```

### 2.3 StreamCallbacks 回调详解

**位置**：`Source/CodeHarness/Llm/ChatProvider.h:17-24`

```cpp
struct StreamCallbacks
{
    // ===== 文本回调 =====
    // 当 LLM 产生新的文本片段时调用
    // 参数 text：新增的文本（不是累积的全文！）
    // 示例：LLM 输出 "Hello world"
    //   回调1: onText("Hello")
    //   回调2: onText(" ")
    //   回调3: onText("world")
    std::function<void(std::string_view)> onText;
    
    // ===== 思考回调 =====
    // 当 LLM 产生思考内容时调用（Claude thinking、OpenAI reasoning）
    std::function<void(std::string_view)> onThink;
    
    // ===== 工具调用回调 =====
    // 工具调用是增量到达的，需要累积
    
    // 工具调用开始：LLM 决定调用某个工具
    // 参数：
    //   index - 工具调用索引（可能有多个并行调用）
    //   id - 调用唯一标识
    //   name - 工具名称
    std::function<void(int index, std::string_view id, std::string_view name)> onToolCallStart;
    
    // 工具调用增量：参数逐步到达
    // 参数：
    //   index - 对应的调用索引
    //   argsChunk - 新增的参数片段（JSON 字符串片段）
    // 示例：参数 {"command": "ls"}
    //   回调1: onToolCallDelta(0, "{\"com")
    //   回调2: onToolCallDelta(0, "mand\":")
    //   回调3: onToolCallDelta(0, " \"ls\"}")
    std::function<void(int index, std::string_view argsChunk)> onToolCallDelta;
    
    // ===== 完成回调 =====
    // LLM 响应结束
    // 参数：
    //   finishReason - 结束原因
    //   usage - token 用量统计
    std::function<void(FinishReason, const TokenUsage&)> onFinish;
};
```

## 3. 类型定义详解

### 3.1 Message（消息）

```cpp
struct Message
{
    // 角色：User（用户）、Assistant（助手）、Tool（工具响应）
    Role role = Role::User;
    
    // 内容：可以是文本或思考
    // vector 因为一条消息可能有多个内容块
    std::vector<ContentPart> content;
    
    // 仅 Tool 角色：关联的工具调用 ID
    std::optional<std::string> toolCallId;
    
    // 仅 Assistant 角色：请求的工具调用
    std::vector<ToolCall> toolCalls;
};
```

**三种角色**：

```
User Message:
  role: User
  content: [TextPart{text: "帮我分析项目"}]
  toolCallId: (无)
  toolCalls: (无)

Assistant Message（带工具调用）:
  role: Assistant
  content: [TextPart{text: "让我先看看目录"}]
  toolCallId: (无)
  toolCalls: [ToolCall{id: "call_1", name: "Bash", arguments: "{\"command\":\"ls\"}"}]

Tool Message:
  role: Tool
  content: [TextPart{text: "文件列表..."}]
  toolCallId: "call_1"  ← 关联到上面的工具调用
  toolCalls: (无)
```

### 3.2 ContentPart（内容部分）

```cpp
// 内容部分：文本或思考
using ContentPart = std::variant<TextPart, ThinkPart>;

struct TextPart
{
    std::string text;  // 普通文本内容
};

struct ThinkPart
{
    std::string think;              // 思考内容
    std::optional<std::string> encrypted;  // 加密的思考内容（某些 Provider）
};
```

### 3.3 ToolCall（工具调用）

```cpp
struct ToolCall
{
    std::string id;         // 唯一标识，用于关联响应
    std::string name;       // 工具名称（如 "Bash"、"Read"）
    std::string arguments;  // 参数（JSON 字符串）
};
```

**为什么 arguments 是字符串而不是 json 对象？**
- API 返回的是字符串
- 可能增量到达
- 需要累积后解析

### 3.4 Tool（工具定义）

```cpp
struct Tool
{
    std::string name;              // 工具名称
    std::string description;       // 工具描述（LLM 会看到）
    nlohmann::json inputSchema;    // 参数 JSON Schema
};
```

**示例**：
```json
{
    "name": "Bash",
    "description": "Execute a shell command",
    "inputSchema": {
        "type": "object",
        "properties": {
            "command": {"type": "string"},
            "timeout": {"type": "integer"}
        },
        "required": ["command"]
    }
}
```

### 3.5 FinishReason（结束原因）

```cpp
enum class FinishReason
{
    Completed,    // 正常结束
    ToolCalls,    // 需要工具调用
    Truncated,    // 被 context window 截断
    Filtered,     // 被安全过滤
    Paused,       // 暂停（某些特殊状态）
    Other,        // 其他
};
```

### 3.6 TokenUsage（用量统计）

```cpp
struct TokenUsage
{
    int64_t inputOther = 0;         // 非缓存的输入 token
    int64_t output = 0;             // 输出 token
    int64_t inputCacheRead = 0;     // 从缓存读取的输入 token
    int64_t inputCacheCreation = 0; // 缓存创建消耗的 token
};
```

**缓存机制**（OpenAI/Anthropic 支持）：
- 系统提示词、历史消息可以缓存
- 缓存命中后，只计 `inputCacheRead`，不计 `inputOther`
- 节省成本

### 3.7 ThinkingEffort（思考强度）

```cpp
enum class ThinkingEffort
{
    Off,    // 关闭思考
    Low,    // 低强度
    Medium, // 中等
    High,   // 高强度
    XHigh,  // 超高
    Max     // 最大
};
```

**映射到各 Provider**：

| CodeHarness | OpenAI | Anthropic | Google |
|-------------|--------|-----------|--------|
| Off | 无 reasoning | disabled | disabled |
| Low | `reasoning_effort: "low"` | `low` | `LOW` |
| Medium | `reasoning_effort: "medium"` | `medium` | `MEDIUM` |
| High | `reasoning_effort: "high"` | `high` | `HIGH` |

### 3.8 ModelCapability（模型能力）

```cpp
struct ModelCapability
{
    bool imageIn = false;       // 支持图片输入
    bool videoIn = false;       // 支持视频输入
    bool audioIn = false;       // 支持音频输入
    bool thinking = false;      // 支持思考模式
    bool toolUse = false;       // 支持工具调用
    int64_t maxContextTokens = 0; // 最大上下文长度
};
```

**能力注册**：
```cpp
// 查询模型能力
ModelCapability GetCapability(std::string_view modelName);

// 示例
GetCapability("gpt-4o");  // {imageIn: true, toolUse: true, maxContextTokens: 128000}
GetCapability("claude-sonnet-4");  // {thinking: true, toolUse: true, ...}
```

## 4. OpenAI Provider 实现

### 4.1 类定义

```cpp
// OpenAI 配置
struct OpenAiConfig
{
    std::string apiKey;              // API Key
    std::string host = "api.openai.com";  // API 主机（可改为兼容 API）
    std::string path = "/v1/chat/completions";  // API 路径
    std::string model;               // 模型名称
    std::optional<ThinkingEffort> thinking;  // 思考模式
    int maxCompletionTokens = 0;     // 最大输出 token（0 表示不限制）
};

class OpenAiProvider : public ChatProvider
{
public:
    // 构造：需要配置和 HTTP 客户端
    // http 是注入的，方便测试时 Mock
    OpenAiProvider(OpenAiConfig _config, HttpClient* _http);
    
    // 实现 ChatProvider 接口
    std::string Name() const override { return "openai"; }
    std::string ModelName() const override { return config.model; }
    std::optional<ThinkingEffort> ThinkingEffortLevel() const override { return config.thinking; }
    
    absl::Status Generate(
        std::string_view systemPrompt,
        std::span<const Tool> tools,
        std::span<const Message> history,
        const StreamCallbacks& callbacks,
        std::stop_token stopToken = {}
    ) override;

private:
    OpenAiConfig config;
    HttpClient* http;  // 非拥有，由外部管理生命周期
};
```

### 4.2 Generate 实现流程

```cpp
absl::Status OpenAiProvider::Generate(
    std::string_view systemPrompt,
    std::span<const Tool> tools,
    std::span<const Message> history,
    const StreamCallbacks& callbacks,
    std::stop_token stopToken
) {
    // 1. 构建 JSON 请求体
    nlohmann::json body;
    body["model"] = config.model;
    body["messages"] = BuildMessages(systemPrompt, history);
    body["stream"] = true;  // 启用流式
    
    if (!tools.empty()) {
        body["tools"] = BuildTools(tools);
    }
    
    if (config.thinking) {
        body["reasoning_effort"] = MapThinkingEffort(*config.thinking);
    }
    
    // 2. 构建 HTTP 请求
    HttpRequest request;
    request.method = "POST";
    request.path = config.path;
    request.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + config.apiKey"},
    };
    request.body = body.dump();
    
    // 3. 发送请求，接收流式响应
    auto response = http->StreamRequest(request, stopToken);
    if (!response.ok()) {
        return response.status();
    }
    
    // 4. 解析 SSE 流
    SseParser parser;
    for (auto& chunk : *response) {
        parser.Feed(chunk);
        
        while (auto event = parser.NextEvent()) {
            // 解析事件数据
            auto data = nlohmann::json::parse(*event);
            
            // 处理增量响应
            ProcessStreamChunk(data, callbacks);
        }
    }
    
    return absl::OkStatus();
}
```

### 4.3 流式响应解析

**OpenAI SSE 格式**：
```
data: {"id":"chatcmpl-123","choices":[{"delta":{"content":"Hello"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","choices":[{"delta":{"content":" world"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","choices":[{"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

**解析逻辑**：
```cpp
void ProcessStreamChunk(const nlohmann::json& data, const StreamCallbacks& callbacks) {
    auto& choices = data["choices"];
    if (choices.empty()) return;
    
    auto& delta = choices[0]["delta"];
    
    // 文本增量
    if (delta.contains("content") && !delta["content"].is_null()) {
        callbacks.onText(delta["content"].get<std::string_view>());
    }
    
    // 思考增量（reasoning_content）
    if (delta.contains("reasoning_content")) {
        callbacks.onThink(delta["reasoning_content"].get<std::string_view>());
    }
    
    // 工具调用增量
    if (delta.contains("tool_calls")) {
        for (auto& tc : delta["tool_calls"]) {
            int index = tc["index"].get<int>();
            
            if (tc.contains("id")) {
                // 工具调用开始
                callbacks.onToolCallStart(index, tc["id"], tc["function"]["name"]);
            }
            
            if (tc.contains("function") && tc["function"].contains("arguments")) {
                // 参数增量
                callbacks.onToolCallDelta(index, tc["function"]["arguments"]);
            }
        }
    }
    
    // 完成原因
    if (choices[0].contains("finish_reason") && !choices[0]["finish_reason"].is_null()) {
        FinishReason reason = MapFinishReason(choices[0]["finish_reason"]);
        TokenUsage usage = ParseUsage(data["usage"]);
        callbacks.onFinish(reason, usage);
    }
}
```

## 5. SSE Parser 实现

### 5.1 SSE 格式说明

**Server-Sent Events (SSE) 格式**：
```
event: message
data: {"content": "hello"}

event: message
data: {"content": "world"}

data: [DONE]
```

**规则**：
- `field: value` 格式
- 空行分隔事件
- `data:` 行累积为事件数据

### 5.2 SseParser 接口

```cpp
class SseParser
{
public:
    // 喂入数据（可能是不完整的）
    void Feed(std::string_view data);
    
    // 提取下一个事件（如果没有完整事件，返回 nullopt）
    std::optional<std::string> NextEvent();
    
    // 是否已收到结束标记
    bool Done() const;
    
    // 重置解析器
    void Reset();

private:
    // 尝试提取一行
    bool TryExtractLine(std::string& line);
    
    std::string buffer;           // 接收缓冲区
    std::string currentEventData; // 当前事件的累积数据
    bool currentEventHasData = false;
    bool done = false;
};
```

### 5.3 使用示例

```cpp
SseParser parser;

// 分块喂入数据（模拟网络接收）
parser.Feed("data: {\"content\":");
parser.Feed("\"hello\"}\n\n");  // 空行结束事件

// 提取事件
auto event = parser.NextEvent();
// event = {"content":"hello"}

// 继续喂入
parser.Feed("data: [DONE]\n\n");
parser.NextEvent();  // 返回 "[DONE]"
parser.Done();  // true
```

## 6. HttpClient 抽象

### 6.1 为什么需要 HttpClient 抽象

```cpp
// 不好的设计：直接使用具体实现
OpenAiProvider provider(config);
provider.Generate(...);  // 内部直接创建 HTTP 连接

// 问题：
// 1. 测试时无法 Mock，需要真实网络
// 2. 无法控制超时、重试等行为
// 3. 无法注入测试数据
```

### 6.2 HttpClient 接口

```cpp
// HTTP 请求结构
struct HttpRequest
{
    std::string method = "GET";
    std::string host;
    std::string path;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 30000;
};

// HTTP 响应结构
struct HttpResponse
{
    int status = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

// 流式响应回调
using StreamCallback = std::function<void(std::string_view chunk)>;

class HttpClient
{
public:
    virtual ~HttpClient() = default;
    
    // 普通请求
    virtual absl::StatusOr<HttpResponse> Request(const HttpRequest& request) = 0;
    
    // 流式请求
    virtual absl::Status StreamRequest(
        const HttpRequest& request,
        StreamCallback callback,
        std::stop_token stopToken = {}
    ) = 0;
};
```

### 6.3 BeastHttpClient 实现

使用 **Boost.Beast + OpenSSL** 实现 HTTPS 客户端：

```cpp
class BeastHttpClient : public HttpClient
{
public:
    absl::StatusOr<HttpResponse> Request(const HttpRequest& request) override {
        // 1. 建立 SSL 连接
        // 2. 发送 HTTP 请求
        // 3. 读取响应
        // 4. 关闭连接
    }
    
    absl::Status StreamRequest(
        const HttpRequest& request,
        StreamCallback callback,
        std::stop_token stopToken
    ) override {
        // 1. 建立 SSL 连接
        // 2. 发送 HTTP 请求
        // 3. 持续读取响应，每次收到 chunk 调用 callback
        // 4. 直到连接关闭或取消
    }
};
```

## 7. 测试分析

### 7.1 OpenAiProviderTest.cpp

**Mock HttpClient**：
```cpp
class MockHttpClient : public HttpClient {
public:
    MOCK_METHOD(absl::StatusOr<HttpResponse>, Request, (const HttpRequest&), (override));
    MOCK_METHOD(absl::Status, StreamRequest, (const HttpRequest&, StreamCallback, std::stop_token), (override));
};

TEST(OpenAiProvider, GenerateWithTextResponse) {
    MockHttpClient mockHttp;
    
    // 准备模拟响应
    std::string sseData = 
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n";
    
    EXPECT_CALL(mockHttp, StreamRequest(testing::_, testing::_, testing::_))
        .WillOnce([&](const HttpRequest&, StreamCallback callback, std::stop_token) {
            callback(sseData);  // 喂入模拟数据
            return absl::OkStatus();
        });
    
    // 创建 Provider
    OpenAiProvider provider({.apiKey = "test", .model = "gpt-4"}, &mockHttp);
    
    // 收集回调结果
    std::string accumulated;
    StreamCallbacks callbacks{
        .onText = [&](std::string_view text) { accumulated += text; }
    };
    
    // 调用
    auto status = provider.Generate("You are helpful", {}, {}, callbacks);
    
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(accumulated, "Hello world");
}
```

### 7.2 SseParserTest.cpp

```cpp
TEST(SseParser, IncrementalParsing) {
    SseParser parser;
    
    // 分块喂入
    parser.Feed("data: {\"a\":");
    EXPECT_FALSE(parser.NextEvent().has_value());  // 不完整
    
    parser.Feed("1}\n\n");
    auto event = parser.NextEvent();
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(*event, "{\"a\":1}");
}

TEST(SseParser, MultipleEvents) {
    SseParser parser;
    parser.Feed("data: event1\n\ndata: event2\n\n");
    
    EXPECT_EQ(*parser.NextEvent(), "event1");
    EXPECT_EQ(*parser.NextEvent(), "event2");
    EXPECT_FALSE(parser.NextEvent().has_value());
}
```

## 8. 类关系图

```
┌───────────────────────────────────────────────────────────────┐
│                       ChatProvider                             │
│                      (interface)                               │
└─────────────────────────────┬─────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          │                   │                   │
          ▼                   ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│ OpenAiProvider  │ │AnthropicProvider│ │  MockProvider   │
└────────┬────────┘ └─────────────────┘ └─────────────────┘
         │
         │ uses
         ▼
┌─────────────────┐
│   HttpClient    │  ← HTTP 客户端接口
│  (interface)    │
└────────┬────────┘
         │
    ┌────┴────┐
    │         │
    ▼         ▼
┌─────────┐ ┌─────────┐
│BeastHttp│ │MockHttp │
│ Client  │ │ Client  │
└─────────┘ └─────────┘

OpenAiProvider 使用：
┌─────────────────┐
│   SseParser     │  ← SSE 流解析
└─────────────────┘
```

## 9. 与其他模块的关系

```
LLM 层被以下模块使用：

Engine 层：
  Loop → ChatProvider.Generate()

Config 层：
  ProviderManager → 创建 ChatProvider 实例

Agent 层：
  Agent → 持有 ChatProvider，传给 TurnInput
```

## 10. 小结

本章我们学习了：

- **为什么需要 LLM 抽象**：统一多 Provider、测试友好、功能统一
- **ChatProvider 接口**：Generate 方法和 StreamCallbacks
- **类型定义**：Message、ContentPart、ToolCall、Tool、FinishReason、TokenUsage
- **OpenAI Provider 实现**：请求构建、SSE 解析、回调触发
- **SseParser**：增量解析 SSE 流
- **HttpClient 抽象**：测试时可 Mock

## 11. 练习建议

1. **阅读源码**：打开 `Llm/OpenAiProvider.cpp`，跟踪一次完整的 Generate 调用
2. **写测试**：尝试写一个测试，验证工具调用的回调顺序
3. **思考题**：如果要添加 Anthropic Provider，哪些代码可以复用？哪些需要新写？

## 12. 下一步

下一章我们将深入 **工具系统**，理解两阶段执行模型。

→ [04-tool-system.md](04-tool-system.md)