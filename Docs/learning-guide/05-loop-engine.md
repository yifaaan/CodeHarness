# 第5章：Loop 引擎深度剖析

> Loop 是 Agent 的"神经系统"，实现 LLM-工具的循环决策。

## 1. 什么是 Loop？

### 1.1 核心思想

Agent 的工作是循环的：

```
1. 用户输入问题
2. LLM 思考，决定做什么
3. 如果需要工具，执行工具
4. 将结果反馈给 LLM
5. LLM 继续思考
6. 重复 3-5，直到 LLM 说"完成"
```

**Loop 的职责**：实现这个循环，但要**无状态**。

### 1.2 为什么无状态？

**有状态的问题**：
```cpp
// ❌ 不好：Loop 有内部状态
class Loop {
    vector<Message> history;  // 内部状态
    int stepCount;            // 内部状态
    
    void runTurn(Message input) {
        history.push(input);
        // ... 复杂的状态管理
    }
};

// 问题：
// 1. 测试困难：需要设置内部状态
// 2. 难以理解：隐藏状态影响行为
// 3. 难以并发：多个 Loop 共享状态？
```

**无状态的优点**：
```cpp
// ✅ 好：Loop 是纯函数
TurnResult RunTurn(TurnInput input) {
    // 所有依赖都通过参数传入
    // 没有内部状态
    // 输入相同 → 输出相同
}
```

## 2. RunTurn 函数详解

### 2.1 函数签名

```cpp
// 位置：Source/CodeHarness/Engine/Loop.h
TurnResult RunTurn(TurnInput input, const LoopHooks& hooks = {});
```

### 2.2 TurnInput 结构

```cpp
// 位置：Source/CodeHarness/Engine/LoopTypes.h
struct TurnInput
{
    // ===== LLM 相关 =====
    llm::ChatProvider* provider = nullptr;  // LLM Provider
    std::string systemPrompt;               // 系统提示词
    std::vector<llm::Message> history;      // 对话历史

    // ===== 工具相关 =====
    std::vector<ExecutableTool*> tools;     // 可用工具列表

    // ===== Host 相关 =====
    host::Host* host = nullptr;             // Host 接口

    // ===== 控制相关 =====
    EventDispatcher dispatchEvent;          // 事件分发器
    std::stop_token stopToken;              // 取消令牌
    int maxSteps = 1000;                    // 最大步数限制

    // ===== 权限相关 =====
    permission::PermissionGate* permissionGate = nullptr;  // 权限门控
};
```

### 2.3 TurnResult 结构

```cpp
struct TurnResult
{
    StopReason stopReason = StopReason::Completed;  // 停止原因
    int stepsExecuted = 0;                          // 执行的步数
    llm::TokenUsage totalUsage;                     // 总 token 用量
    std::vector<llm::Message> updatedHistory;       // 更新后的历史
    std::string errorMessage;                       // 错误信息（如果有）
};

// 停止原因枚举
enum class StopReason
{
    Completed,  // 正常完成
    MaxSteps,   // 达到最大步数
    Aborted,    // 被取消
    Error,      // 发生错误
};
```

### 2.4 LoopHooks 结构

```cpp
// 扩展钩子：可以在特定时机注入自定义逻辑
struct LoopHooks
{
    // 每一步开始前调用
    // 返回 HookResult 可以阻止继续
    std::function<absl::StatusOr<HookResult>(int step)> beforeStep;
    
    // 每一步结束后调用
    std::function<void(int step)> afterStep;
    
    // 当 LLM 非工具调用结束时，决定是否继续
    // 参数：stopReason（"end_turn"、"max_tokens"、"other"）
    // 返回：true 继续，false 停止
    std::function<bool(std::string_view stopReason)> shouldContinueAfterStop;
};
```

## 3. RunTurn 实现详解

### 3.1 整体流程

**位置**：`Source/CodeHarness/Engine/Loop.cpp`

```cpp
TurnResult RunTurn(TurnInput input, const LoopHooks& hooks)
{
    // 初始化结果
    TurnResult result;
    result.updatedHistory = std::move(input.history);

    // 构建工具定义列表（给 LLM 看）
    std::vector<llm::Tool> toolDefs;
    for (auto* t : input.tools) {
        toolDefs.push_back(t->GetToolDefinition());
    }

    // 主循环
    for (int step = 1; step <= input.maxSteps; ++step) {
        // ... 每一步的逻辑
    }

    // 达到最大步数
    result.stopReason = StopReason::MaxSteps;
    return result;
}
```

### 3.2 每一步的流程

```cpp
for (int step = 1; step <= input.maxSteps; ++step)
{
    // ===== 1. 检查取消 =====
    if (input.stopToken.stop_requested()) {
        result.stopReason = StopReason::Aborted;
        return result;
    }

    // ===== 2. 钩子：步开始 =====
    if (hooks.beforeStep) {
        hooks.beforeStep(step);
    }

    // ===== 3. 发送事件 =====
    Dispatch(input, StepStartedEvent{step});

    // ===== 4. 调用 LLM =====
    // 准备回调
    std::string assistantText;
    std::vector<llm::ToolCall> pendingCalls;
    llm::FinishReason finishReason = llm::FinishReason::Other;
    llm::TokenUsage stepUsage{};

    llm::StreamCallbacks callbacks{
        .onText = [&](std::string_view text) {
            assistantText += text;
            Dispatch(input, AssistantDeltaEvent{std::string(text)});
        },
        .onToolCallStart = [&](int idx, std::string_view id, std::string_view name) {
            if (idx >= static_cast<int>(pendingCalls.size())) {
                pendingCalls.resize(idx + 1);
            }
            pendingCalls[idx].id = std::string(id);
            pendingCalls[idx].name = std::string(name);
        },
        .onToolCallDelta = [&](int idx, std::string_view args) {
            if (idx < static_cast<int>(pendingCalls.size())) {
                pendingCalls[idx].arguments += args;
            }
        },
        .onFinish = [&](llm::FinishReason f, const llm::TokenUsage& u) {
            finishReason = f;
            stepUsage = u;
        },
    };

    // 调用 LLM
    auto status = input.provider->Generate(
        input.systemPrompt,
        toolDefs,
        result.updatedHistory,
        callbacks,
        input.stopToken
    );

    // ===== 5. 处理错误 =====
    if (!status.ok()) {
        result.stopReason = StopReason::Error;
        result.errorMessage = std::string(status.message());
        Dispatch(input, ErrorEvent{result.errorMessage});
        return result;
    }

    // ===== 6. 累积用量 =====
    result.totalUsage.output += stepUsage.output;
    result.totalUsage.inputOther += stepUsage.inputOther;
    result.totalUsage.inputCacheRead += stepUsage.inputCacheRead;
    result.totalUsage.inputCacheCreation += stepUsage.inputCacheCreation;

    // ===== 7. 清理无效工具调用 =====
    pendingCalls.erase(
        std::remove_if(pendingCalls.begin(), pendingCalls.end(),
            [](const llm::ToolCall& tc) { return tc.name.empty(); }
        ),
        pendingCalls.end()
    );

    // ===== 8. 构建助手消息 =====
    llm::Message assistantMsg;
    assistantMsg.role = llm::Role::Assistant;
    if (!assistantText.empty()) {
        assistantMsg.content.push_back(llm::TextPart{std::move(assistantText)});
    }
    assistantMsg.toolCalls = pendingCalls;
    result.updatedHistory.push_back(std::move(assistantMsg));

    result.stepsExecuted = step;

    // ===== 9. 发送事件 =====
    Dispatch(input, StepCompletedEvent{step});
    if (hooks.afterStep) {
        hooks.afterStep(step);
    }

    // ===== 10. 检查是否结束 =====
    bool hasToolCalls = !pendingCalls.empty();
    if (finishReason != llm::FinishReason::ToolCalls || !hasToolCalls) {
        // 不是工具调用，可能结束
        if (hooks.shouldContinueAfterStop) {
            std::string reasonStr = 
                finishReason == llm::FinishReason::Truncated ? "max_tokens"
                : finishReason == llm::FinishReason::Completed ? "end_turn"
                : "other";
            if (hooks.shouldContinueAfterStop(reasonStr)) {
                continue;  // 钩子说继续
            }
        }
        result.stopReason = StopReason::Completed;
        return result;
    }

    // ===== 11. 执行工具调用 =====
    ToolContext ctx{.host = input.host, .stopToken = input.stopToken};

    for (const auto& tc : pendingCalls) {
        // 检查取消
        if (input.stopToken.stop_requested()) {
            result.stopReason = StopReason::Aborted;
            return result;
        }

        // 解析参数
        nlohmann::json args;
        if (!tc.arguments.empty()) {
            try {
                args = nlohmann::json::parse(tc.arguments);
            } catch (...) {
                args = nlohmann::json::object();
            }
        }

        // 发送事件
        Dispatch(input, ToolCallStartedEvent{tc.id, tc.name, args});

        // 查找工具
        auto* tool = FindTool(input.tools, tc.name);
        ToolResult toolResult;
        if (!tool) {
            toolResult.isError = true;
            toolResult.content = fmt::format("tool '{}' not found", tc.name);
        } else {
            // 执行工具（包含权限检查）
            toolResult = ExecuteToolCall(*tool, tc, ctx, input);
        }

        // 发送结果事件
        Dispatch(input, ToolResultEvent{tc.id, tc.name, toolResult});

        // 构建工具消息
        llm::Message toolMsg;
        toolMsg.role = llm::Role::Tool;
        toolMsg.toolCallId = tc.id;
        toolMsg.content.push_back(llm::TextPart{toolResult.content});
        result.updatedHistory.push_back(std::move(toolMsg));
    }
    // 继续下一轮循环...
}
```

## 4. 事件系统

### 4.1 事件类型

```cpp
// 事件变体：所有可能的事件
using LoopEvent = std::variant<
    StepStartedEvent,        // 步开始
    StepCompletedEvent,      // 步结束
    AssistantDeltaEvent,     // LLM 文本增量
    ToolCallStartedEvent,    // 工具调用开始
    ToolResultEvent,         // 工具调用结果
    PermissionRequestedEvent,// 权限请求
    PermissionDeniedEvent,   // 权限拒绝
    ErrorEvent               // 错误
>;

// 各事件结构
struct StepStartedEvent { int step; };
struct StepCompletedEvent { int step; };
struct AssistantDeltaEvent { std::string text; };
struct ToolCallStartedEvent {
    std::string id;
    std::string name;
    nlohmann::json args;
};
struct ToolResultEvent {
    std::string id;
    std::string name;
    ToolResult result;
};
struct PermissionRequestedEvent {
    std::string toolName;
    nlohmann::json args;
    std::string description;
};
struct PermissionDeniedEvent {
    std::string toolName;
    std::string description;
};
struct ErrorEvent { std::string message; };
```

### 4.2 事件分发

```cpp
// 事件分发器类型
using EventDispatcher = std::function<void(const LoopEvent&)>;

// 内部分发函数
void Dispatch(const TurnInput& input, LoopEvent event) {
    if (input.dispatchEvent) {
        input.dispatchEvent(event);
    }
}
```

### 4.3 事件流向

```
Loop 内部产生事件
        ↓
EventDispatcher(input.dispatchEvent)
        ↓
┌───────────────────┬───────────────────┐
│                   │                   │
▼                   ▼                   ▼
AgentRecords     TUI / CLI            日志
(wire.jsonl)     (实时显示)           (spdlog)

// AgentRecords 记录事件
records->Log(RecordType::ContextAppendLoopEvent, event);

// TUI 渲染事件
switch (event.index()) {
    case AssistantDelta: renderText(get<AssistantDeltaEvent>(event).text);
    case ToolResult: renderToolResult(...);
}
```

## 5. 取消机制

### 5.1 stop_token 使用

```cpp
// 外部请求取消
std::stop_source stopSource;
std::stop_token stopToken = stopSource.get_token();

// 启动任务
std::thread([&]() {
    TurnInput input;
    input.stopToken = stopToken;
    RunTurn(input);
}).detach();

// 请求取消
stopSource.request_stop();

// Loop 内部检查
if (input.stopToken.stop_requested()) {
    result.stopReason = StopReason::Aborted;
    return result;  // 立即返回
}
```

### 5.2 检查时机

```cpp
// 在多个关键点检查
for (int step = 1; step <= input.maxSteps; ++step) {
    // 1. 步开始时
    if (input.stopToken.stop_requested()) { return Aborted; }
    
    // ... 调用 LLM ...
    
    // 2. 每个工具调用前
    for (const auto& tc : pendingCalls) {
        if (input.stopToken.stop_requested()) { return Aborted; }
        // 执行工具...
    }
}
```

## 6. 工具执行流程

### 6.1 ExecuteToolCall 函数

**位置**：`Source/CodeHarness/Engine/Loop.cpp:28-70`

```cpp
ToolResult ExecuteToolCall(
    ExecutableTool& tool,
    const llm::ToolCall& tc,
    const ToolContext& ctx,
    const TurnInput& input
) {
    // ===== 1. 解析参数 =====
    nlohmann::json args;
    if (!tc.arguments.empty()) {
        try {
            args = nlohmann::json::parse(tc.arguments);
        } catch (const nlohmann::json::parse_error& e) {
            return {.content = fmt::format("invalid tool arguments: {}", e.what()), .isError = true};
        }
    }

    // ===== 2. 阶段1：验证 =====
    auto resolution = tool.ResolveExecution(args);
    if (!resolution.ok()) {
        return {.content = std::string(resolution.status().message()), .isError = true};
    }

    const auto& exec = *resolution;

    // ===== 3. 权限检查 =====
    if (input.permissionGate != nullptr && exec.requiresPermission) {
        // 发送权限请求事件
        Dispatch(input, PermissionRequestedEvent{tc.name, args, exec.description});
        
        // 询问权限门控
        if (!input.permissionGate->ShouldRun(true, tc.name, args, exec.description)) {
            // 权限被拒绝
            Dispatch(input, PermissionDeniedEvent{tc.name, exec.description});
            return {.content = fmt::format("permission denied for tool '{}'", tc.name), .isError = true};
        }
    }

    // ===== 4. 阶段2：执行 =====
    auto result = tool.Execute(args, ctx);
    if (!result.ok()) {
        return {.content = std::string(result.status().message()), .isError = true};
    }

    return std::move(*result);
}
```

### 6.2 流程图

```
工具调用请求 (ToolCall)
        ↓
┌───────────────────────────────────┐
│ 1. 解析参数 (JSON parse)          │
│    失败 → 返回错误                 │
└───────────────┬───────────────────┘
                ↓
┌───────────────────────────────────┐
│ 2. 验证阶段 (ResolveExecution)    │
│    失败 → 返回错误                 │
└───────────────┬───────────────────┘
                ↓
┌───────────────────────────────────┐
│ 3. 检查是否需要权限                │
│    requiresPermission?            │
└───────────────┬───────────────────┘
                │
        ┌───────┴───────┐
        │ Yes           │ No
        ↓               ↓
┌───────────────┐ ┌───────────────┐
│ 权限门控检查   │ │ 跳过权限检查  │
│ 拒绝 → 返回   │ │              │
└───────┬───────┘ └───────┬───────┘
        │                 │
        └────────┬────────┘
                 ↓
┌───────────────────────────────────┐
│ 4. 执行阶段 (Execute)             │
│    成功/失败 → 返回结果            │
└───────────────────────────────────┘
```

## 7. 测试分析

### 7.1 Mock ChatProvider

```cpp
// Mock ChatProvider
class MockChatProvider : public llm::ChatProvider {
public:
    // 预设响应
    struct CannedResponse {
        std::string text;
        std::vector<llm::ToolCall> toolCalls;
        llm::FinishReason finishReason = llm::FinishReason::Completed;
    };
    
    std::vector<CannedResponse> responses;
    int callCount = 0;
    
    absl::Status Generate(
        std::string_view systemPrompt,
        std::span<const llm::Tool> tools,
        std::span<const llm::Message> history,
        const llm::StreamCallbacks& callbacks,
        std::stop_token stopToken
    ) override {
        if (callCount >= responses.size()) {
            return absl::NotFoundError("no more canned responses");
        }
        
        auto& resp = responses[callCount++];
        
        // 模拟流式回调
        if (!resp.text.empty()) {
            callbacks.onText(resp.text);
        }
        
        for (int i = 0; i < resp.toolCalls.size(); ++i) {
            auto& tc = resp.toolCalls[i];
            callbacks.onToolCallStart(i, tc.id, tc.name);
            callbacks.onToolCallDelta(i, tc.arguments);
        }
        
        callbacks.onFinish(resp.finishReason, {});
        return absl::OkStatus();
    }
};
```

### 7.2 Mock Tool

```cpp
class MockTool : public engine::ExecutableTool {
public:
    std::function<absl::StatusOr<ToolExecution>(const nlohmann::json&)> resolveHandler;
    std::function<absl::StatusOr<ToolResult>(const nlohmann::json&)> executeHandler;
    
    std::string Name() const override { return "Mock"; }
    std::string Description() const override { return "Mock tool"; }
    nlohmann::json Parameters() const override { return {}; }
    
    absl::StatusOr<ToolExecution> ResolveExecution(const nlohmann::json& args) override {
        return resolveHandler ? resolveHandler(args) : ToolExecution{};
    }
    
    absl::StatusOr<ToolResult> Execute(const nlohmann::json& args, const ToolContext& ctx) override {
        return executeHandler ? executeHandler(args) : ToolResult{.content = "ok"};
    }
};
```

### 7.3 测试用例

```cpp
TEST(Loop, SingleTextResponse) {
    MockChatProvider provider;
    provider.responses = {
        {.text = "Hello!", .finishReason = llm::FinishReason::Completed}
    };
    
    TurnInput input;
    input.provider = &provider;
    input.maxSteps = 10;
    
    auto result = RunTurn(input);
    
    EXPECT_EQ(result.stopReason, StopReason::Completed);
    EXPECT_EQ(result.stepsExecuted, 1);
    EXPECT_EQ(result.updatedHistory.size(), 1);  // 一条助手消息
}

TEST(Loop, ToolCallThenComplete) {
    MockChatProvider provider;
    provider.responses = {
        {
            .toolCalls = {llm::ToolCall{"1", "Mock", "{}"}},
            .finishReason = llm::FinishReason::ToolCalls
        },
        {.text = "Done!", .finishReason = llm::FinishReason::Completed}
    };
    
    MockTool tool;
    tool.executeHandler = [](const nlohmann::json&) {
        return ToolResult{.content = "tool result"};
    };
    
    TurnInput input;
    input.provider = &provider;
    input.tools = {&tool};
    input.maxSteps = 10;
    
    auto result = RunTurn(input);
    
    EXPECT_EQ(result.stopReason, StopReason::Completed);
    EXPECT_EQ(result.stepsExecuted, 2);  // 两步
}

TEST(Loop, Cancellation) {
    MockChatProvider provider;
    provider.responses.resize(100);  // 很多响应
    
    std::stop_source stopSource;
    
    TurnInput input;
    input.provider = &provider;
    input.stopToken = stopSource.get_token();
    input.maxSteps = 1000;
    
    // 在另一个线程请求取消
    std::thread([&]() {
        std::this_thread::sleep_for(100ms);
        stopSource.request_stop();
    }).detach();
    
    auto result = RunTurn(input);
    
    EXPECT_EQ(result.stopReason, StopReason::Aborted);
}
```

## 8. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                          RunTurn(input, hooks)                       │
│                              (纯函数)                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  输入: TurnInput                                                     │
│  ├── provider: ChatProvider*                                         │
│  ├── tools: vector<ExecutableTool*>                                  │
│  ├── host: Host*                                                     │
│  ├── history: vector<Message>                                        │
│  ├── dispatchEvent: EventDispatcher                                  │
│  ├── stopToken: stop_token                                           │
│  └── permissionGate: PermissionGate*                                 │
│                                                                      │
│  输出: TurnResult                                                    │
│  ├── stopReason: StopReason                                          │
│  ├── stepsExecuted: int                                              │
│  ├── totalUsage: TokenUsage                                          │
│  └── updatedHistory: vector<Message>                                 │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    │ 调用
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       ChatProvider                                   │
│                       .Generate()                                    │
│                                                                      │
│  流式回调:                                                           │
│  onText ────────────► AssistantDeltaEvent                           │
│  onToolCallStart ───► 构建 pendingCalls                              │
│  onToolCallDelta ───► 累积 arguments                                 │
│  onFinish ──────────► 记录 finishReason                              │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    │ 工具调用
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    ExecuteToolCall(tool, tc, ctx, input)            │
│                                                                      │
│  1. 解析参数                                                         │
│  2. ResolveExecution() → 验证                                        │
│  3. 权限检查 (如果需要)                                              │
│  4. Execute() → 执行                                                 │
│                                                                      │
│  事件:                                                               │
│  PermissionRequestedEvent (如果需要权限)                             │
│  PermissionDeniedEvent (如果拒绝)                                    │
│  ToolResultEvent (执行结果)                                          │
└─────────────────────────────────────────────────────────────────────┘
```

## 9. 与其他模块的关系

```
Loop 被以下模块使用：

Agent 层：
  Agent.Prompt() → 构建 TurnInput → RunTurn()

依赖：
  Loop → ChatProvider (调用 LLM)
  Loop → ExecutableTool (执行工具)
  Loop → Host (传给工具)
  Loop → PermissionGate (权限检查)
```

## 10. 小结

本章我们学习了：

- **为什么无状态**：测试友好、易于理解、并发安全
- **RunTurn 函数**：输入 TurnInput，输出 TurnResult
- **主循环流程**：检查取消 → 调用 LLM → 处理工具调用 → 重复
- **事件系统**：8 种事件类型，分发到 UI 和记录
- **取消机制**：stop_token 检查，多处验证
- **工具执行**：两阶段 + 权限检查

## 11. 练习建议

1. **阅读源码**：打开 `Engine/Loop.cpp`，逐行理解主循环
2. **写测试**：写一个测试，验证多步工具调用的场景
3. **思考题**：如果 LLM 返回了 ToolCall 但 finishReason 不是 ToolCalls，会发生什么？

## 12. 下一步

下一章我们将深入 **Agent 核心**，理解如何将所有模块组合起来。

→ [06-agent-core.md](06-agent-core.md)