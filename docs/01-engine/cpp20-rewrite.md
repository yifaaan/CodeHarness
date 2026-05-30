# Engine C++20 重写方案

本文件给出 `engine` 模块的 C++20 实现建议。

## 目标

第一版 engine 要做到：

- 管理对话历史。
- 调用统一 provider 接口。
- 消费 streaming event。
- 执行 tool calls。
- 把 tool results 回填给模型。
- 输出统一 `StreamEvent` 给 CLI/TUI。
- 能处理错误、权限拒绝、max turns。

不要在第一版做完整 auto compact、复杂 cost 统计、图像预处理。先保留接口。

## 目录建议

```text
src/codeharness/engine/
  conversation.hpp
  conversation.cpp
  stream_event.hpp
  query_context.hpp
  query_engine.hpp
  query_engine.cpp
  tool_executor.hpp
  tool_executor.cpp
  cost_tracker.hpp
```

## 消息模型

Python 的 Pydantic union 在 C++ 中建议用 `std::variant`。

```cpp
struct TextBlock {
    std::string text;
};

struct ImageBlock {
    std::string mediaType;
    std::string dataBase64;
    std::optional<std::string> sourcePath;
};

struct ToolUseBlock {
    std::string id;
    std::string name;
    nlohmann::json input;
};

struct ToolResultBlock {
    std::string toolUseId;
    std::string content;
    bool isError = false;
    nlohmann::json metadata = nlohmann::json::object();
};

using ContentBlock = std::variant<TextBlock, ImageBlock, ToolUseBlock, ToolResultBlock>;

enum class Role {
    User,
    Assistant
};

struct ConversationMessage {
    Role role;
    std::vector<ContentBlock> content;
};
```

## StreamEvent 设计

```cpp
struct AssistantTextDelta {
    std::string text;
};

struct AssistantTurnComplete {
    ConversationMessage message;
    nlohmann::json usage;
};

struct ToolExecutionStarted {
    std::string toolUseId;
    std::string toolName;
    nlohmann::json input;
};

struct ToolExecutionCompleted {
    std::string toolUseId;
    std::string toolName;
    std::string output;
    bool isError = false;
    nlohmann::json metadata;
};

struct StatusEvent {
    std::string message;
};

struct ErrorEvent {
    std::string message;
    bool fatal = false;
};

using StreamEvent = std::variant<
    AssistantTextDelta,
    AssistantTurnComplete,
    ToolExecutionStarted,
    ToolExecutionCompleted,
    StatusEvent,
    ErrorEvent
>;
```

## Provider 接口

Engine 只依赖统一接口：

```cpp
class IStreamingClient {
public:
    virtual ~IStreamingClient() = default;

    virtual void streamMessage(
        const ApiMessageRequest& request,
        std::function<void(ApiStreamEvent)> onEvent
    ) = 0;
};
```

如果后续使用 coroutine，可以改成 generator 或 async task，但第一版 callback 更容易。

## QueryContext

```cpp
struct QueryContext {
    IStreamingClient& apiClient;
    ToolRegistry& toolRegistry;
    PermissionChecker& permissionChecker;
    std::filesystem::path cwd;
    std::string model;
    std::string systemPrompt;
    int maxTokens = 4096;
    std::optional<int> maxTurns = 200;
    HookExecutor* hooks = nullptr;
    nlohmann::json* toolMetadata = nullptr;
    std::function<bool(std::string_view tool, std::string_view reason)> permissionPrompt;
};
```

## QueryEngine 类

```cpp
class QueryEngine {
public:
    QueryEngine(QueryContext context);

    void submitMessage(
        std::string text,
        std::function<void(StreamEvent)> emit
    );

    const std::vector<ConversationMessage>& messages() const;
    void loadMessages(std::vector<ConversationMessage> messages);
    void clear();

private:
    QueryContext context_;
    std::vector<ConversationMessage> messages_;
    nlohmann::json toolMetadata_;
};
```

第一版可以让 `submitMessage` 阻塞执行，边执行边调用 `emit`。后续再改成后台线程或 coroutine。

## ToolExecutor

把工具执行从 QueryEngine 中拆出，避免 `query_engine.cpp` 变成巨型文件。

```cpp
class ToolExecutor {
public:
    ToolExecutor(ToolRegistry& registry,
                 PermissionChecker& permissions,
                 HookExecutor* hooks);

    ToolResultBlock execute(
        const ToolUseBlock& call,
        const ToolExecutionContext& ctx,
        std::function<void(StreamEvent)> emit
    );
};
```

职责：

- 查找工具。
- 校验输入。
- 执行 pre hook。
- 做权限检查。
- 调用工具。
- 截断大输出。
- 执行 post hook。
- 返回 `ToolResultBlock`。

## run loop 伪代码

```cpp
void QueryEngine::submitMessage(std::string text, Emit emit) {
    messages_.push_back(userTextMessage(text));

    int turn = 0;
    while (true) {
        if (context_.maxTurns && turn++ >= *context_.maxTurns) {
            emit(ErrorEvent{"Exceeded max turns", true});
            return;
        }

        ApiMessageRequest request = buildRequest(messages_, context_);
        ProviderResult providerResult;

        context_.apiClient.streamMessage(request, [&](ApiStreamEvent ev) {
            if (auto* delta = std::get_if<ApiTextDelta>(&ev)) {
                emit(AssistantTextDelta{delta->text});
            }
            if (auto* done = std::get_if<ApiMessageComplete>(&ev)) {
                providerResult.message = done->message;
                providerResult.usage = done->usage;
            }
        });

        messages_.push_back(providerResult.message);
        emit(AssistantTurnComplete{providerResult.message, providerResult.usage});

        auto calls = extractToolUses(providerResult.message);
        if (calls.empty()) {
            break;
        }

        std::vector<ToolResultBlock> results;
        for (const auto& call : calls) {
            results.push_back(toolExecutor.execute(call, toolCtx, emit));
        }

        messages_.push_back(userToolResultsMessage(results));
    }
}
```

## 并发执行策略

第一版可以顺序执行工具，保证逻辑正确。第二版再加入并发。

并发版本建议：

- 用 `ThreadPool` 投递工具任务。
- 结果用 vector 按原 index 保存。
- 所有任务完成后一次性追加 tool result message。
- 工具开始和结束事件仍实时 emit。

```cpp
std::vector<std::future<ToolResultBlock>> futures;
for (const auto& call : calls) {
    futures.push_back(pool.submit([&, call] {
        return executor.execute(call, ctx, emit);
    }));
}

for (auto& future : futures) {
    results.push_back(future.get());
}
```

注意：`emit` 如果被多个线程调用，必须加锁或把事件投递到线程安全队列。

## 错误处理

建议区分三类错误：

| 错误 | 处理 |
| --- | --- |
| Provider fatal error | 发 `ErrorEvent{fatal=true}`，停止本轮 |
| Tool execution error | 变成 `ToolResultBlock{isError=true}`，继续回给模型 |
| Permission denied | 变成 error tool result，继续回给模型 |

不要让某个工具异常直接终止进程。

## JSON 序列化

所有消息和事件都应支持 JSON 序列化，方便：

- session 保存。
- stream-json 输出。
- UI backend-only 协议。
- 单元测试快照。

建议为每个 block 加 `type` 字段：

```json
{"type":"text","text":"hello"}
{"type":"tool_use","id":"...","name":"read_file","input":{}}
```

## 测试建议

写一个 fake provider，不需要真实网络：

```cpp
class FakeStreamingClient : public IStreamingClient {
public:
    std::vector<ApiStreamEvent> scriptedEvents;
    void streamMessage(const ApiMessageRequest&, Callback cb) override {
        for (auto& ev : scriptedEvents) cb(ev);
    }
};
```

测试用 fake provider 返回 tool call，然后检查：

- 工具被调用。
- messages 里追加了 assistant message。
- messages 里追加了 tool result user message。
- emit 收到 tool_started 和 tool_completed。
