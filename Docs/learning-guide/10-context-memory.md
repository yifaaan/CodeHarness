# 第10章：Context 模块详解

> Context 模块管理对话历史的 Token 估算，并在接近 Context Window 限制时自动压缩。

## 1. 为什么需要 Context 模块？

### 1.1 Context Window 限制问题

LLM 有一个 **Context Window**（上下文窗口），限制了它能处理的最大 token 数量：

```
GPT-4:        128,000 tokens
Claude-3:     200,000 tokens
GPT-3.5:      16,000 tokens

问题：对话历史不断增长
  - 用户消息 → Assistant 回复 → Tool 结果 → Assistant 回复...
  - 每轮对话增加几百到几千 token
  - 不加以控制，很快超出限制
  - 结果：截断、错误、丢失上下文
```

### 1.2 裸 vector 的问题

```cpp
// ❌ 问题设计：裸 vector
class Agent {
    std::vector<llm::Message> history;
    
    // 每次检查 token 需要重新计算
    int64_t countTokens() {
        int64_t total = 0;
        for (auto& msg : history) {
            total += estimateTokens(msg);  // O(n) 每次调用
        }
        return total;
    }
};

// 问题：
// 1. 每次 Prompt 都要遍历整个历史
// 2. 不知道当前用了多少 token
// 3. 没有压缩机制
```

### 1.3 Context 模块的解决方案

```cpp
// ✅ 好：使用 ContextMemory
class Agent {
    context::ContextMemory history;  // 拥有 token 缓存
    
    // O(1) 获取 token 数量
    int64_t tokens = history.TokenCount();
    
    // 自动压缩
    if (ShouldCompact(tokens, cfg)) {
        Compact(provider, history, cfg);
    }
};
```

**好处**：
1. **Token 缓存**：O(1) 查询，不需要每次重新计算
2. **自动压缩**：接近限制时让 LLM 总结旧消息
3. **透明集成**：Loop 代码完全不需要改动

## 2. ContextMemory 类详解

### 2.1 类定义

**位置**：`Source/CodeHarness/Context/ContextMemory.h`

```cpp
// 拥有对话历史加上缓存的 token 估算
// Agent 使用这个代替裸 std::vector<llm::Message>
// 可以 O(1) 查询"context 用了多少空间"
class ContextMemory
{
public:
    ContextMemory() = default;

    // ===== 添加消息 =====
    
    // 追加消息并更新缓存的 token 数量
    // 内部调用 TokenEstimate::EstimateTokens(msg)
    void Append(llm::Message msg);
    
    // ===== 替换历史 =====
    
    // 替换整个历史（压缩后使用）
    // 从头重新计算 token 数量
    void ReplaceAll(std::vector<llm::Message> msgs);
    
    // ===== 清空 =====
    
    void Clear();

    // ===== 查询 =====
    
    // 只读访问底层消息
    const std::vector<llm::Message>& Messages() const { return messages; }
    
    // 缓存的 token 估算（访问时不重新计算）
    int64_t TokenCount() const { return tokens; }
    
    bool Empty() const { return messages.empty(); }
    std::size_t Size() const { return messages.size(); }

private:
    std::vector<llm::Message> messages;  // 对话历史
    int64_t tokens = 0;                   // 缓存的 token 估算
};
```

### 2.2 Append 实现

```cpp
void ContextMemory::Append(llm::Message msg)
{
    // 计算新消息的 token 数量
    int64_t msgTokens = TokenEstimate::EstimateTokens(msg);
    
    // 追加消息
    messages.push_back(std::move(msg));
    
    // 更新缓存
    tokens += msgTokens;
}
```

### 2.3 ReplaceAll 实现

```cpp
void ContextMemory::ReplaceAll(std::vector<llm::Message> msgs)
{
    // 从头重新计算 token
    int64_t total = 0;
    for (const auto& msg : msgs) {
        total += TokenEstimate::EstimateTokens(msg);
    }
    
    // 替换
    messages = std::move(msgs);
    tokens = total;
}
```

## 3. TokenEstimate 启发式估算

### 3.1 为什么是"估算"？

**问题**：不同 LLM 有不同的 tokenizer
- GPT-4 使用 `tiktoken`
- Claude 使用不同的 tokenizer
- 没有统一的 token 计算方法

**解决方案**：使用启发式估算
- 简单快速
- 不依赖具体 tokenizer
- 足够准确用于判断是否需要压缩

### 3.2 估算算法

**位置**：`Source/CodeHarness/Context/TokenEstimate.h`

```cpp
// 启发式估算：chars/4 + 每消息开销
// 这个是保守估计，实际可能更少
namespace TokenEstimate
{
    // ===== 文本估算 =====
    
    // 纯文本的 token 估算
    // 规则：字符数 / 4（平均每个 token 约 4 个字符）
    int64_t EstimateTokens(std::string_view text) {
        return static_cast<int64_t>(text.size()) / 4 + 1;
    }
    
    // ===== 消息估算 =====
    
    // 单条消息的 token 估算
    // 包括：内容 + 角色 + 工具调用开销
    int64_t EstimateTokens(const llm::Message& msg) {
        int64_t total = 0;
        
        // 每条消息有固定开销（角色、格式等）
        total += 4;  // 保守估计
        
        // 内容部分
        for (const auto& part : msg.content) {
            std::visit([&](auto&& p) {
                using T = std::decay_t<decltype(p)>;
                if constexpr (std::is_same_v<T, llm::TextPart>) {
                    total += EstimateTokens(p.text);
                } else if constexpr (std::is_same_v<T, llm::ThinkPart>) {
                    total += EstimateTokens(p.think);
                }
            }, part);
        }
        
        // 工具调用开销
        for (const auto& tc : msg.toolCalls) {
            total += 10;  // id + name 开销
            total += EstimateTokens(tc.arguments);
        }
        
        // 工具消息有 toolCallId 开销
        if (msg.toolCallId) {
            total += 5;
        }
        
        return total;
    }
    
    // ===== 消息数组估算 =====
    
    // 多条消息的 token 估算
    int64_t EstimateTokens(std::span<const llm::Message> msgs) {
        int64_t total = 0;
        for (const auto& msg : msgs) {
            total += EstimateTokens(msg);
        }
        return total;
    }
}
```

### 3.3 估算示例

```
文本: "Hello world"
  → 11 chars / 4 = 2.75 → 约 3 tokens

消息: User: "分析这个项目"
  → 4 (开销) + 7 chars/4 = 约 6 tokens

消息: Assistant: "让我先看看..." + ToolCall{Bash, args}
  → 4 (开销) + 文本 tokens + 10 (工具开销) + args tokens
  
消息: Tool: "文件列表..."
  → 4 (开销) + 5 (toolCallId) + 内容 tokens
```

## 4. Compactor 压缩器

### 4.1 压缩原理

```
原始历史（超过 75% 限制）:
  [Message1, Message2, ..., Message50]
        ↓
压缩流程:
  1. 让 LLM 总结前 40 条消息（Message1..Message40）
  2. 生成摘要："用户请求分析项目，Agent 执行了 ls、cat 等命令..."
  3. 保留后 10 条消息（Message41..Message50）
        ↓
压缩后历史:
  [SummaryMessage, Message41, Message42, ..., Message50]
        ↓
效果:
  - 原来可能 100,000 tokens
  - 压缩后可能 20,000 tokens
  - 释放空间给新对话
```

### 4.2 CompactionConfig

```cpp
// 压缩配置
struct CompactionConfig
{
    // 最大 context token 数量
    // 0 表示禁用压缩（从 LLM Capability 获取）
    int64_t maxContextTokens = 0;
    
    // 压缩触发阈值
    // 当使用量超过此比例时触发压缩
    // 默认 0.75 = 75%
    double compactThreshold = 0.75;
    
    // 保留的消息数量
    // 压缩时保留最后 N 条消息不压缩
    // 默认 10 条
    int retainTail = 10;
};
```

### 4.3 ShouldCompact 判断

```cpp
// 判断是否需要压缩
bool ShouldCompact(int64_t usedTokens, const CompactionConfig& cfg)
{
    // 未配置最大值 → 禁用
    if (cfg.maxContextTokens <= 0) {
        return false;
    }
    
    // 未超过阈值 → 不需要压缩
    int64_t threshold = static_cast<int64_t>(cfg.maxContextTokens * cfg.compactThreshold);
    if (usedTokens < threshold) {
        return false;
    }
    
    // 需要压缩
    return true;
}
```

### 4.4 Compact 执行

```cpp
// 执行压缩
// 返回 nullopt 表示没有足够消息可压缩（非错误）
// 返回错误表示 LLM 调用失败
absl::StatusOr<std::optional<CompactionResult>> Compact(
    llm::ChatProvider* provider,
    std::span<const llm::Message> history,
    const CompactionConfig& cfg,
    std::stop_token stopToken = {}
) {
    // ===== 1. 检查是否有足够消息 =====
    if (history.size() <= static_cast<std::size_t>(cfg.retainTail + 1)) {
        // 消息太少，没有意义压缩
        return std::nullopt;
    }
    
    // ===== 2. 分离前缀和尾部 =====
    // 前缀：需要总结的消息
    std::size_t splitPoint = history.size() - cfg.retainTail;
    auto prefix = history.first(splitPoint);
    auto tail = history.last(cfg.retainTail);
    
    // ===== 3. 构建总结提示词 =====
    std::string summaryPrompt = BuildSummaryPrompt(prefix);
    
    // ===== 4. 调用 LLM 生成摘要 =====
    std::string summary;
    llm::StreamCallbacks callbacks{
        .onText = [&](std::string_view text) { summary += text; }
    };
    
    auto status = provider->Generate(
        "Summarize the conversation history concisely.",
        {},  // 无工具
        BuildSummaryMessages(prefix),
        callbacks,
        stopToken
    );
    
    if (!status.ok()) {
        return status;
    }
    
    // ===== 5. 返回结果 =====
    CompactionResult result;
    result.summary = std::move(summary);
    result.removedCount = static_cast<int>(splitPoint);
    
    // 估算压缩后的 token 数量
    auto compacted = BuildCompactedHistory(result.summary, history, cfg.retainTail);
    result.newTokenCount = TokenEstimate::EstimateTokens(compacted);
    
    return result;
}
```

### 4.5 BuildCompactedHistory

```cpp
// 构建压缩后的历史
// 一条摘要消息 + 保留的尾部消息
std::vector<llm::Message> BuildCompactedHistory(
    std::string_view summary,
    std::span<const llm::Message> history,
    int retainTail
) {
    std::vector<llm::Message> result;
    
    // 1. 添加摘要消息
    llm::Message summaryMsg;
    summaryMsg.role = llm::Role::Assistant;  // 作为系统信息
    summaryMsg.content.push_back(llm::TextPart{
        fmt::format("[Summary of earlier conversation]\n{}", summary)
    });
    result.push_back(std::move(summaryMsg));
    
    // 2. 添加保留的尾部消息
    auto tail = history.last(retainTail);
    for (const auto& msg : tail) {
        result.push_back(msg);
    }
    
    return result;
}
```

## 5. 与 Agent 的集成

### 5.1 Agent 使用 ContextMemory

```cpp
// Agent.h
class Agent {
    // 使用 ContextMemory 代替裸 vector
    context::ContextMemory history;
    
    // 压缩配置
    std::optional<context::CompactionConfig> compactionConfig;
};
```

### 5.2 Prompt 中的压缩流程

```cpp
absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
{
    // ... 前置检查 ...
    
    // ===== Context 压缩检查 =====
    
    // 1. 获取模型的 context window 大小
    auto capability = llm::GetCapability(provider->ModelName());
    int64_t maxTokens = compactionConfig 
        ? compactionConfig->maxContextTokens 
        : capability.maxContextTokens;
    
    // 2. 配置压缩参数
    context::CompactionConfig cfg;
    cfg.maxContextTokens = maxTokens;
    cfg.compactThreshold = compactionConfig 
        ? compactionConfig->compactThreshold 
        : 0.75;
    cfg.retainTail = compactionConfig 
        ? compactionConfig->retainTail 
        : 10;
    
    // 3. 计算当前用量 + 新消息
    int64_t usedTokens = history.TokenCount();
    int64_t incomingTokens = TokenEstimate::EstimateTokens(text) + 4;
    
    // 4. 检查是否需要压缩
    if (context::ShouldCompact(usedTokens + incomingTokens, cfg)) {
        // 发送压缩开始事件
        Dispatch(ContextCompactingEvent{});
        
        // 执行压缩
        auto compactResult = context::Compact(
            provider,
            history.Messages(),
            cfg,
            currentStopSource->get_token()
        );
        
        if (compactResult.ok() && *compactResult) {
            // 替换历史
            auto newHistory = context::BuildCompactedHistory(
                (*compactResult)->summary,
                history.Messages(),
                cfg.retainTail
            );
            history.ReplaceAll(std::move(newHistory));
        }
        
        // 发送压缩完成事件
        Dispatch(ContextCompactedEvent{});
    }
    
    // ===== 继续正常流程 =====
    
    // 添加用户消息
    history.Append(userMsg);
    
    // 构建 TurnInput
    input.history = history.Messages();
    
    // ... 调用 Loop ...
}
```

### 5.3 Resume 中的处理

```cpp
absl::Status Agent::Resume()
{
    if (!records) {
        return absl::FailedPreconditionError("No records set");
    }
    
    auto allRecords = records->ReadAll();
    if (!allRecords.ok()) {
        return allRecords.status();
    }
    
    // 使用 ContextMemory::Append 保持 token 缓存一致
    for (const auto& record : *allRecords) {
        std::visit(overloaded{
            [&](const ContextAppendMessageRecord& r) {
                history.Append(r.message);  // 自动更新 token 缓存
            },
            // ...
        }, record.record);
    }
    
    return absl::OkStatus();
}
```

## 6. 测试分析

### 6.1 TokenEstimate 测试

```cpp
TEST(TokenEstimate, TextEstimation) {
    EXPECT_EQ(TokenEstimate::EstimateTokens(""), 1);
    EXPECT_EQ(TokenEstimate::EstimateTokens("Hello"), 2);
    EXPECT_EQ(TokenEstimate::EstimateTokens("Hello world"), 3);
}

TEST(TokenEstimate, MessageEstimation) {
    llm::Message msg;
    msg.role = llm::Role::User;
    msg.content.push_back(llm::TextPart{"Hello world"});
    
    auto tokens = TokenEstimate::EstimateTokens(msg);
    EXPECT_GE(tokens, 7);  // 4 (开销) + 3 (内容)
}
```

### 6.2 ContextMemory 测试

```cpp
TEST(ContextMemory, AppendUpdatesCache) {
    ContextMemory mem;
    
    llm::Message msg;
    msg.content.push_back(llm::TextPart{"Hello"});
    
    mem.Append(msg);
    
    EXPECT_EQ(mem.Size(), 1);
    EXPECT_GT(mem.TokenCount(), 0);  // 缓存已更新
}

TEST(ContextMemory, ReplaceAllRecomputesTokens) {
    ContextMemory mem;
    
    // 添加一些消息
    mem.Append(Message{...});
    mem.Append(Message{...});
    int64_t oldTokens = mem.TokenCount();
    
    // 替换
    std::vector<llm::Message> newHistory;
    newHistory.push_back(Message{...});
    
    mem.ReplaceAll(std::move(newHistory));
    
    EXPECT_EQ(mem.Size(), 1);
    EXPECT_NE(mem.TokenCount(), oldTokens);  // 重新计算了
}
```

### 6.3 Compactor 测试

```cpp
TEST(Compactor, ShouldCompact) {
    CompactionConfig cfg;
    cfg.maxContextTokens = 100000;
    cfg.compactThreshold = 0.75;
    
    EXPECT_FALSE(ShouldCompact(50000, cfg));   // 50% < 75%
    EXPECT_FALSE(ShouldCompact(74000, cfg));   // 74% < 75%
    EXPECT_TRUE(ShouldCompact(75000, cfg));    // 75% = 75%
    EXPECT_TRUE(ShouldCompact(80000, cfg));    // 80% > 75%
}

TEST(Compactor, ShouldCompactDisabled) {
    CompactionConfig cfg;
    cfg.maxContextTokens = 0;  // 禁用
    
    EXPECT_FALSE(ShouldCompact(1000000, cfg));  // 任何值都不压缩
}

TEST(Compactor, CompactWithMockProvider) {
    MockChatProvider provider;
    provider.responses = {
        {.text = "User asked to analyze project. Agent ran ls command.", .finishReason = llm::FinishReason::Completed}
    };
    
    std::vector<llm::Message> history;
    for (int i = 0; i < 20; ++i) {
        history.push_back(Message{...});
    }
    
    CompactionConfig cfg;
    cfg.retainTail = 10;
    
    auto result = Compact(&provider, history, cfg);
    
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(*result);
    EXPECT_EQ((*result)->removedCount, 10);
    EXPECT_EQ((*result)->newTokenCount, ...);
}
```

## 7. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                      ContextMemory                                   │
├─────────────────────────────────────────────────────────────────────┤
│  - messages: vector<Message>    // 对话历史                          │
│  - tokens: int64_t              // 缓存的 token 估算                 │
│                                                                      │
│  + Append(msg)                 // 添加 + 更新缓存                     │
│  + ReplaceAll(msgs)            // 替换 + 重算缓存                     │
│  + Clear()                     // 清空                               │
│  + Messages() → span           // 只读访问                           │
│  + TokenCount() → int64_t      // O(1) 查询                          │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    │ 使用
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      TokenEstimate                                   │
├─────────────────────────────────────────────────────────────────────┤
│  + EstimateTokens(text) → int64_t                                   │
│  + EstimateTokens(Message) → int64_t                                │
│  + EstimateTokens(span<Message>) → int64_t                          │
│                                                                      │
│  算法: chars/4 + 每消息开销                                          │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        Compactor                                     │
├─────────────────────────────────────────────────────────────────────┤
│  + ShouldCompact(usedTokens, cfg) → bool                            │
│  + Compact(provider, history, cfg) → CompactionResult               │
│  + BuildCompactedHistory(summary, history, retainTail) → vector     │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    │ 调用
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     ChatProvider                                     │
│                     (用于生成摘要)                                    │
└─────────────────────────────────────────────────────────────────────┘

Agent 持有:
┌─────────────────────────────────────────────────────────────────────┐
│                          Agent                                       │
├─────────────────────────────────────────────────────────────────────┤
│  - history: ContextMemory                                           │
│  - compactionConfig: optional<CompactionConfig>                     │
│                                                                      │
│  + SetCompactionConfig(cfg)                                         │
│                                                                      │
│  Prompt() 中:                                                        │
│    if ShouldCompact():                                              │
│        Compact() → ReplaceAll()                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 8. 与其他模块的关系

```
Context 模块的位置：

被使用：
  Agent → ContextMemory (历史存储)
  Agent → Compactor (压缩检查)
  Agent → TokenEstimate (估算)

依赖：
  Context → llm::Message (消息类型)
  Context → llm::ChatProvider (压缩时调用 LLM)

关键点：
  - Loop 完全不知道 Context 模块
  - Agent 在 Prompt 前检查并压缩
  - 压缩是"提前"的，不是"事后"的
```

## 9. 小结

本章我们学习了：

- **为什么需要 Context 模块**：Context Window 限制、Token 估算需求
- **ContextMemory 类**：消息存储 + Token 缓存，O(1) 查询
- **TokenEstimate**：启发式估算算法（chars/4 + 开销）
- **Compactor**：ShouldCompact 判断、Compact 执行、BuildCompactedHistory
- **与 Agent 的集成**：Prompt 前检查、Resume 时保持缓存一致

**关键设计点**：
1. **Loop 无感知**：压缩在 Agent 层完成，Loop 收到的已经是压缩后的历史
2. **提前压缩**：在超过阈值时压缩，不是等到超限后截断
3. **保留尾部**：最近的消息不压缩，保持上下文连贯

## 10. 练习建议

1. **阅读源码**：打开 `Context/Compactor.cpp`，看完整压缩流程
2. **写测试**：测试不同消息数量的压缩效果
3. **思考题**：如果 retainTail 设置为 0，会发生什么问题？

## 11. 下一步

下一章我们将学习 **Hooks 模块**，理解生命周期钩子的扩展机制。

→ [11-hooks-system.md](11-hooks-system.md)