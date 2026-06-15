# 第0章：LLM Agent 前置知识

> 在深入 CodeHarness 实现之前，我们需要先理解 LLM Agent 的核心概念。

## 1. 什么是 LLM Agent？

传统程序：代码 → 固定逻辑 → 输出

LLM Agent：用户请求 → LLM推理 → 决策 → 执行工具 → 反馈 → 继续推理...

**核心区别**：LLM Agent 不是按固定逻辑运行，而是由 LLM 动态决定下一步做什么。

```
┌─────────────────────────────────────────────────────────────┐
│                      LLM Agent 工作模式                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   用户: "帮我分析这个项目的结构"                              │
│                    ↓                                        │
│   LLM: 接收请求，分析意图                                    │
│                    ↓                                        │
│   LLM: "我需要先看看目录结构，我要调用 Bash 工具"             │
│                    ↓                                        │
│   执行工具: Bash("ls -la")                                   │
│                    ↓                                        │
│   工具返回: 目录列表内容                                     │
│                    ↓                                        │
│   LLM: 分析结果，"我还需要看主要源文件"                       │
│                    ↓                                        │
│   执行工具: Read("src/main.cpp")                            │
│                    ↓                                        │
│   工具返回: 文件内容                                         │
│                    ↓                                        │
│   LLM: 整合信息，生成最终回答                                │
│                    ↓                                        │
│   用户: 收到完整分析                                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**类比**：
- LLM 是"大脑"，负责思考和决策
- Tools 是"手脚"，负责实际操作（读文件、执行命令、搜索）
- Loop 是"神经系统"，连接大脑和手脚，循环运作

## 2. 核心概念详解

### 2.1 Message（消息）

Agent 与 LLM 之间通过 **Message** 通信。一次对话包含多条消息，形成 **History**（对话历史）。

```
History = [
    Message { role: "user",    content: "帮我分析项目" },
    Message { role: "assistant", content: "好的，让我先看看目录", toolCalls: [...] },
    Message { role: "tool",    content: "目录内容...", toolCallId: "call_001" },
    Message { role: "assistant", content: "现在让我看看源代码", toolCalls: [...] },
    Message { role: "tool",    content: "源代码内容...", toolCallId: "call_002" },
    Message { role: "assistant", content: "分析结果如下..." },
]
```

**三种角色**：
- `User`：用户输入
- `Assistant`：LLM 的响应
- `Tool`：工具执行的结果

**CodeHarness 中的定义**（`Llm/Types.h:60-66`）：

```cpp
// 消息结构体，表示对话中的一条消息
struct Message
{
    // 消息角色：User（用户）、Assistant（助手）、Tool（工具响应）
    Role role = Role::User;
    
    // 消息内容，可以是文本或思考内容
    // vector 表示一条消息可能有多个内容块
    std::vector<ContentPart> content;
    
    // 仅用于 Tool 角色：关联的工具调用 ID
    std::optional<std::string> toolCallId;
    
    // 仅用于 Assistant 角色：LLM 请求的工具调用列表
    std::vector<ToolCall> toolCalls;
};
```

### 2.2 ToolCall（工具调用）

当 LLM 决定要执行某个操作时，它会返回一个 **ToolCall**。

```
ToolCall {
    id: "call_abc123",           // 唯一标识，用于关联响应
    name: "Bash",                // 工具名称
    arguments: '{"command": "ls -la"}'  // JSON格式的参数
}
```

**CodeHarness 中的定义**（`Llm/Types.h:53-58`）：

```cpp
// 工具调用结构体，表示 LLM 请求执行某个工具
struct ToolCall
{
    // 唯一标识符，用于将工具执行结果与原调用关联
    std::string id;
    
    // 工具名称，如 "Bash"、"Read"、"Write"
    std::string name;
    
    // 工具参数，JSON 字符串格式
    // 注意：这里存的是字符串，需要解析成 JSON 对象才能使用
    std::string arguments;
};
```

**流程**：
1. LLM 返回 ToolCall
2. Agent 解析参数
3. 执行工具
4. 将结果作为 Tool 角色的 Message 返回给 LLM

### 2.3 Streaming（流式响应）

LLM 响应通常是**流式的**——不是一次性返回全部内容，而是逐步返回。

**为什么需要流式？**
- 用户体验：实时看到内容生成，不用等待
- 长响应：避免内存占用过大
- 工具调用：可能包含多个工具调用，逐个处理

**流式回调模式**（`Llm/ChatProvider.h:17-24`）：

```cpp
// 流式回调结构体，用于处理 LLM 的增量响应
struct StreamCallbacks
{
    // 文本增量回调：每当 LLM 产生新的文本片段时调用
    // 参数 text 是新增的文本片段（不是累积的全文）
    std::function<void(std::string_view)> onText;
    
    // 思考增量回调：某些模型（如 Claude）有 thinking 模块
    std::function<void(std::string_view)> onThink;
    
    // 工具调用开始回调：LLM 开始请求某个工具
    // 参数：index（工具索引）、id（调用ID）、name（工具名）
    std::function<void(int index, std::string_view id, std::string_view name)> onToolCallStart;
    
    // 工具调用增量回调：参数逐步累积
    // 参数 argsChunk 是新增的参数片段
    std::function<void(int index, std::string_view argsChunk)> onToolCallDelta;
    
    // 完成回调：LLM 响应结束
    // 参数：finishReason（结束原因）、usage（token用量统计）
    std::function<void(FinishReason, const TokenUsage&)> onFinish;
};
```

**使用示例**（`Engine/Loop.cpp:114-142`）：

```cpp
// 在 Loop 中创建回调，累积响应内容
std::string assistantText;  // 累积的文本内容
std::vector<llm::ToolCall> pendingCalls;  // 累积的工具调用

llm::StreamCallbacks callbacks{
    // 文本增量：拼接文本 + 发送事件
    .onText = [&](std::string_view text) {
        assistantText += text;  // 累积文本
        Dispatch(input, AssistantDeltaEvent{std::string(text)});  // 发送事件给UI
    },
    
    // 工具调用开始：在 pendingCalls 中创建槽位
    .onToolCallStart = [&](int idx, std::string_view id, std::string_view name) {
        if (idx >= static_cast<int>(pendingCalls.size())) {
            pendingCalls.resize(idx + 1);  // 扩容
        }
        pendingCalls[idx].id = std::string(id);
        pendingCalls[idx].name = std::string(name);
    },
    
    // 工具调用增量：拼接参数
    .onToolCallDelta = [&](int idx, std::string_view args) {
        if (idx < static_cast<int>(pendingCalls.size())) {
            pendingCalls[idx].arguments += args;  // 参数是逐步到达的JSON片段
        }
    },
    
    // 完成：记录结束原因和token用量
    .onFinish = [&](llm::FinishReason f, const llm::TokenUsage& u) {
        finishReason = f;
        stepUsage = u;
    },
};
```

### 2.4 Context Window（上下文窗口）

LLM 有一个 **Context Window**（上下文窗口），限制了它能看到的历史消息长度。

```
Context Window = 128K tokens（GPT-4）
                 200K tokens（Claude）

如果对话历史超过限制，会发生：
1. 最老的消息被截断
2. LLM 可能丢失之前的上下文
3. 导致 Agent 行为异常
```

**CodeHarness 的解决方案**：
- **Context Compaction**（上下文压缩）：当接近限制时，让 LLM 总结旧消息，替换为摘要
- 这个功能在第8章详细讲解

### 2.5 Finish Reason（结束原因）

LLM 响应结束时，会返回一个 **Finish Reason**，告诉 Agent 为什么结束。

**CodeHarness 中的定义**（`Llm/Types.h:20-28`）：

```cpp
// 响应结束原因枚举
enum class FinishReason
{
    Completed,    // 正常结束：LLM 完成回答，没有更多操作
    ToolCalls,    // 工具调用：LLM 请求执行工具，需要处理
    Truncated,    // 截断：超出 context window，部分内容被丢弃
    Filtered,     // 过滤：内容被安全过滤器拦截
    Paused,       // 暂停：某些特殊模型状态
    Other,        // 其他未知原因
};
```

**Loop 中的处理逻辑**（`Engine/Loop.cpp:178-192`）：

```cpp
// 根据结束原因决定下一步
if (finishReason != llm::FinishReason::ToolCalls || !hasToolCalls)
{
    // 不是工具调用，可能是正常结束或截断
    // 尝试通过钩子判断是否继续
    if (hooks.shouldContinueAfterStop)
    {
        std::string reasonStr = finishReason == llm::FinishReason::Truncated   ? "max_tokens"
                                : finishReason == llm::FinishReason::Completed ? "end_turn"
                                                                               : "other";
        if (hooks.shouldContinueAfterStop(reasonStr))
        {
            continue;  // 钩子说继续，那就继续下一轮
        }
    }
    // 正常结束
    result.stopReason = StopReason::Completed;
    return result;
}
// 是工具调用，继续执行工具...
```

### 2.6 Token Usage（Token 用量）

每次 LLM 调用都会消耗 Token，需要统计以便：
- 控制成本
- 防止超限
- 显示给用户

**CodeHarness 中的定义**（`Llm/Types.h:75-81`）：

```cpp
// Token 使用统计
struct TokenUsage
{
    // 输入 Token（非缓存部分）
    int64_t inputOther = 0;
    
    // 输出 Token
    int64_t output = 0;
    
    // 从缓存读取的输入 Token（某些 Provider 支持缓存）
    int64_t inputCacheRead = 0;
    
    // 缓存创建消耗的 Token
    int64_t inputCacheCreation = 0;
};
```

**Loop 中的累积逻辑**（`Engine/Loop.cpp:155-158`）：

```cpp
// 累积每一步的 token 用量
result.totalUsage.output += stepUsage.output;
result.totalUsage.inputOther += stepUsage.inputOther;
result.totalUsage.inputCacheRead += stepUsage.inputCacheRead;
result.totalUsage.inputCacheCreation += stepUsage.inputCacheCreation;
```

## 3. 为什么需要这些抽象？

### 3.1 多 Provider 兼容

不同 LLM Provider 的 API 差异很大：

| Provider | API | 工具调用格式 | 流式格式 |
|----------|-----|-------------|----------|
| OpenAI | HTTP JSON | `tool_calls` 数组 | SSE |
| Anthropic | HTTP JSON | `content` 中的 `tool_use` | SSE |
| Google GenAI | HTTP JSON | `functionCall` | 分块 |

**CodeHarness 的做法**：
- 定义统一的 `ChatProvider` 接口
- 每个 Provider 实现适配器，将 API 响应转换为统一格式
- 第3章详细讲解

### 3.2 会话持久化

Agent 会话可能很长，需要：
- 保存历史，以便恢复
- 记录操作，用于调试
- 支持断点续传

**CodeHarness 的做法**：
- **Event Sourcing**：所有操作记录为事件
- `wire.jsonl`：每行一个 JSON 事件
- 恢复时：重新播放所有事件

### 3.3 安全控制

Agent 可以执行命令、修改文件，存在风险：
- 误删重要文件
- 执行恶意命令（如果用户输入被注入）

**CodeHarness 的做法**：
- **Two-Phase Execution**：先验证，再执行
- **Permission Gate**：危险操作需要用户确认
- 第7章详细讲解

## 4. C++ 技术栈说明

### 4.1 错误处理：absl::Status / StatusOr

CodeHarness 使用 Google 的 **Abseil** 库处理错误，而不是抛异常。

```cpp
// 传统方式：抛异常
int readFile(string path) {
    if (!exists(path)) throw FileNotFound();
    return read(path);
}

// Abseil 方式：返回 StatusOr
absl::StatusOr<int> readFile(string path) {
    if (!exists(path)) {
        return absl::NotFoundError("file not found");
    }
    return read(path);  // 成功时直接返回值
}

// 使用方式
auto result = readFile("test.txt");
if (!result.ok()) {
    // 错误处理
    cerr << result.status().message();
} else {
    // 使用值
    int content = *result;
}
```

**优点**：
- 无异常开销
- 强制检查错误（忘记检查会编译警告）
- 错误信息结构化

### 4.2 取消机制：std::stop_token

C++20 引入了 **std::stop_token**，用于线程取消。

```cpp
// 创建停止源
std::stop_source stopSource;
std::stop_token stopToken = stopSource.get_token();

// 在循环中检查
while (!stopToken.stop_requested()) {
    // 执行任务...
}

// 外部请求停止
stopSource.request_stop();
```

**CodeHarness 中的使用**：
- Agent.Cancel() → stopSource.request_stop()
- Loop 中检查 stopToken.stop_requested()，及时退出

### 4.3 JSON 处理：nlohmann::json

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 解析
json j = json::parse("{\"name\": \"test\"}");

// 访问
string name = j["name"];  // "test"

// 构建
json args;
args["command"] = "ls -la";
args["timeout"] = 60000;

// 序列化
string s = args.dump();  // "{\"command\":\"ls -la\",\"timeout\":60000}"
```

### 4.4 日志：spdlog

```cpp
#include <spdlog/spdlog.h>

// 不同级别日志
spdlog::info("Agent started");
spdlog::warn("tool not found: {}", toolName);
spdlog::error("LLM call failed: {}", errorMsg);
```

## 5. 小结

现在你应该理解了：

- **LLM Agent 是什么**：由 LLM 动态决策的工具执行系统
- **核心概念**：
  - Message：对话消息
  - ToolCall：LLM 请求的工具调用
  - Streaming：增量响应
  - Context Window：上下文限制
  - Finish Reason：响应结束原因
  - Token Usage：用量统计
- **为什么需要抽象**：兼容多 Provider、持久化、安全控制
- **C++ 技术栈**：absl::Status、std::stop_token、nlohmann::json、spdlog

## 6. 练习建议

1. **概念巩固**：用自己的话解释 ToolCall 的 id、name、arguments 三个字段的用途
2. **代码阅读**：打开 `Llm/Types.h`，对照本章讲解，理解每个字段的含义
3. **思考题**：如果 LLM 返回的 FinishReason 是 Truncated，Agent 应该怎么处理？

## 7. 下一步

下一章我们将学习 **系统架构总览**，理解 CodeHarness 的整体分层设计和8大设计原则。

→ [01-architecture-overview.md](01-architecture-overview.md)