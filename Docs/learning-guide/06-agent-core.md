# 第6章：Agent 核心深度剖析

> Agent 是"组合根"，协调所有子系统，提供用户友好的 API。

## 1. Agent 的角色

### 1.1 什么是组合根？

**组合根（Composition Root）** 是一个设计模式概念：
- 创建和组装所有依赖
- 管理对象生命周期
- 提供统一入口

**Agent 的职责**：
```
用户
  ↓
Agent.Prompt("帮我分析项目")
  ↓
Agent 协调：
  ├── ChatProvider (调用 LLM)
  ├── ToolManager (管理工具)
  ├── Host (文件/进程)
  ├── PermissionGate (权限)
  ├── AgentRecords (记录)
  ├── ContextMemory (上下文管理)
  └── HookEngine (生命周期钩子)
  ↓
Loop.RunTurn()
  ↓
返回结果给用户
```

### 1.2 为什么需要 Agent 层？

**没有 Agent 层的问题**：
```cpp
// 用户需要手动组装所有依赖
ChatProvider* provider = createProvider();
ToolManager* tools = createTools();
Host* host = new LocalHost();
PermissionGate* gate = new PermissionGate();

TurnInput input;
input.provider = provider;
input.tools = tools->LoopTools();
input.host = host;
input.history = history;
input.permissionGate = gate;
// ... 设置更多字段

auto result = RunTurn(input);
// ... 处理结果
```

**有 Agent 层**：
```cpp
Agent agent(provider, host, tools);
agent.SetPermissionMode(PermissionMode::Manual);
agent.SetApprovalCallback(askUser);

auto result = agent.Prompt("帮我分析项目");
// 简洁！
```

## 2. Agent 类详解

### 2.1 类定义

**位置**：`Source/CodeHarness/Agent/Agent.h`

```cpp
class Agent
{
public:
    // ===== 构造 =====
    
    // 构造函数：注入依赖
    // 注意：provider、host、toolManager 是非拥有的（指针，不负责释放）
    Agent(
        llm::ChatProvider* provider,
        host::Host* host = nullptr,
        tools::ToolManager* toolManager = nullptr,
        AgentConfig config = {}
    );
    
    // 禁止拷贝
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    // ===== 核心方法 =====
    
    // 发起对话
    // 参数：用户输入文本
    // 返回：执行结果
    absl::StatusOr<PromptResult> Prompt(std::string_view text);
    
    // 取消当前执行
    void Cancel();
    
    // 清空对话历史
    void ClearContext();
    
    // 设置系统提示词
    void SetSystemPrompt(std::string systemPrompt);
    
    // 设置活跃工具列表
    absl::Status SetActiveTools(std::vector<std::string> tools);

    // ===== 状态查询 =====
    
    // 获取活跃工具列表
    std::vector<std::string> GetActiveTools() const;
    
    // 获取对话历史
    const std::vector<llm::Message>& GetHistory() const;
    
    // 获取当前状态
    AgentStatus GetStatus() const;
    
    // 获取配置
    const AgentConfig& GetConfig() const;

    // ===== 事件分发 =====
    
    // 设置事件分发器
    void SetEventDispatcher(EventDispatcher dispatcher);

    // ===== 权限相关 =====
    
    // 设置权限模式
    void SetPermissionMode(config::PermissionMode mode);
    
    // 设置审批回调
    void SetApprovalCallback(permission::ApprovalCallback callback);

    // ===== 记录相关 =====
    
    // 设置事件溯源记录器
    void SetRecords(records::AgentRecords* records);
    
    // 从记录恢复状态
    absl::Status Resume();
    
    // ===== Context 相关 =====
    
    // 设置压缩配置（覆盖从 LLM Capability 获取的默认值）
    void SetCompactionConfig(context::CompactionConfig cfg);
    
    // ===== Hooks 相关 =====
    
    // 设置钩子引擎
    void SetHookEngine(hooks::HookEngine* engine);

private:
    // ===== 内部方法 =====
    
    // 构建 Loop 所需的工具列表
    absl::StatusOr<std::vector<engine::ExecutableTool*>> BuildLoopTools() const;
    
    // 分发事件
    void Dispatch(const AgentEvent& event) const;
    
    // 设置状态
    void SetStatus(AgentStatus status);
    
    // 生成下一个 Turn ID
    std::string NextTurnId();

    // ===== 成员变量 =====
    
    // 外部依赖（非拥有）
    llm::ChatProvider* provider = nullptr;
    host::Host* host = nullptr;
    tools::ToolManager* toolManager = nullptr;
    
    // 配置
    AgentConfig config;
    
    // 状态
    context::ContextMemory history;  // 对话历史（使用 ContextMemory 而非裸 vector）
    std::vector<std::string> activeTools;  // 活跃工具列表
    AgentStatus status = AgentStatus::Idle;  // 当前状态
    EventDispatcher dispatchEvent;  // 事件分发器
    records::AgentRecords* records = nullptr;  // 记录器
    
    // Context 压缩
    std::optional<context::CompactionConfig> compactionConfig;
    
    // Hooks
    hooks::HookEngine* hookEngine = nullptr;

    // 权限
    std::unique_ptr<permission::PermissionGate> permissionGate;
    permission::ApprovalCallback approvalCallback;
    std::optional<config::PermissionMode> permissionMode;

    // 执行控制
    std::optional<std::stop_source> currentStopSource;  // 当前执行的停止源
    std::uint64_t nextTurnId = 1;  // Turn ID 计数器
};
```

### 2.2 构造函数

```cpp
Agent::Agent(
    llm::ChatProvider* provider,
    host::Host* host,
    tools::ToolManager* toolManager,
    AgentConfig config
) :
    provider(provider),
    host(host),
    toolManager(toolManager),
    config(std::move(config))
{
    // 设置默认系统提示词
    if (this->config.systemPrompt.empty()) {
        this->config.systemPrompt = "You are a helpful AI assistant.";
    }
    
    // 设置默认活跃工具（如果有 ToolManager）
    if (toolManager && this->config.profile.tools.empty()) {
        // 默认使用所有注册的工具
        auto tools = toolManager->LoopTools();
        for (auto* t : tools) {
            this->activeTools.push_back(t->Name());
        }
    }
}
```

## 3. 核心方法实现

### 3.1 Prompt 方法

```cpp
absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
{
    // ===== 1. 检查状态 =====
    if (status == AgentStatus::Running) {
        return absl::FailedPreconditionError("Agent is already running");
    }

    // ===== 2. 生成 Turn ID =====
    std::string turnId = NextTurnId();

    // ===== 3. 创建停止源 =====
    currentStopSource.emplace();
    SetStatus(AgentStatus::Running);

    // ===== 4. 发送事件 =====
    Dispatch(TurnStartedEvent{turnId});
    if (records) {
        records->Log(RecordType::TurnPrompt, {
            {"turnId", turnId},
            {"input", std::string(text)},
            {"origin", "user"}
        });
    }

    // ===== 5. 构建用户消息 =====
    llm::Message userMsg;
    userMsg.role = llm::Role::User;
    userMsg.content.push_back(llm::TextPart{std::string(text)});
    history.push_back(userMsg);

    if (records) {
        records->Log(RecordType::ContextAppendMessage, {{"message", MessageToJson(userMsg)}});
    }

    // ===== 6. 构建 TurnInput =====
    auto toolsResult = BuildLoopTools();
    if (!toolsResult.ok()) {
        SetStatus(AgentStatus::Idle);
        return toolsResult.status();
    }

    engine::TurnInput input;
    input.provider = provider;
    input.tools = *toolsResult;
    input.host = host;
    input.systemPrompt = config.systemPrompt;
    input.history = history;
    input.maxSteps = config.maxSteps;
    input.stopToken = currentStopSource->get_token();
    input.permissionGate = permissionGate.get();

    // 设置事件分发器：将 Loop 事件包装为 Agent 事件
    input.dispatchEvent = [this](const engine::LoopEvent& event) {
        Dispatch(LoopEvent{event});
        if (records) {
            records->Log(RecordType::ContextAppendLoopEvent, {{"event", LoopEventToJson(event)}});
        }
    };

    // ===== 7. 执行 Loop =====
    auto loopResult = engine::RunTurn(input, {});

    // ===== 8. 处理结果 =====
    if (loopResult.stopReason == engine::StopReason::Aborted) {
        SetStatus(AgentStatus::Cancelling);
    } else {
        SetStatus(AgentStatus::Idle);
    }

    // 更新历史
    history = std::move(loopResult.updatedHistory);

    // ===== 9. 构建返回结果 =====
    PromptResult result;
    result.turnId = turnId;
    result.stopReason = loopResult.stopReason;
    result.stepsExecuted = loopResult.stepsExecuted;
    result.usage = loopResult.totalUsage;
    result.errorMessage = loopResult.errorMessage;

    Dispatch(TurnEndedEvent{result});
    currentStopSource.reset();

    return result;
}
```

### 3.2 Cancel 方法

```cpp
void Agent::Cancel()
{
    if (status != AgentStatus::Running) {
        return;  // 没在运行，忽略
    }

    if (currentStopSource) {
        currentStopSource->request_stop();  // 请求停止
    }
    
    if (records) {
        records->Log(RecordType::TurnCancel, {});
    }
}
```

### 3.3 BuildLoopTools 方法

```cpp
absl::StatusOr<std::vector<engine::ExecutableTool*>> Agent::BuildLoopTools() const
{
    if (!toolManager) {
        return std::vector<engine::ExecutableTool*>{};
    }

    std::vector<engine::ExecutableTool*> result;
    
    for (const auto& name : activeTools) {
        auto* tool = toolManager->Find(name);
        if (!tool) {
            return absl::NotFoundError(fmt::format("Tool '{}' not found", name));
        }
        result.push_back(tool);
    }

    return result;
}
```

## 4. 类型定义

### 4.1 AgentStatus

```cpp
enum class AgentStatus
{
    Idle,       // 空闲，可以接受新请求
    Running,    // 正在执行
    Cancelling, // 正在取消（等待 Loop 响应）
};
```

### 4.2 AgentConfig

```cpp
struct AgentProfile
{
    std::string name = "default";        // Profile 名称
    std::string systemPrompt;            // 系统提示词
    std::vector<std::string> tools;      // 默认工具列表
};

struct AgentConfig
{
    std::string systemPrompt;            // 系统提示词（覆盖 profile）
    int maxSteps = 1000;                 // 最大步数
    AgentProfile profile;                // Profile 配置
};
```

### 4.3 PromptResult

```cpp
struct PromptResult
{
    std::string turnId;                  // Turn ID
    engine::StopReason stopReason;       // 停止原因
    int stepsExecuted;                   // 执行的步数
    llm::TokenUsage usage;               // Token 用量
    std::string errorMessage;            // 错误信息
};
```

### 4.4 AgentEvent

```cpp
// Agent 事件类型
struct TurnStartedEvent { std::string turnId; };
struct LoopEvent { engine::LoopEvent event; };
struct TurnEndedEvent { PromptResult result; };
struct StatusChangedEvent { AgentStatus status; };
struct ErrorEvent { std::string message; };

// 事件变体
using AgentEvent = std::variant<
    TurnStartedEvent,
    LoopEvent,
    TurnEndedEvent,
    StatusChangedEvent,
    ErrorEvent
>;
```

## 5. 权限集成

### 5.1 设置权限模式

```cpp
void Agent::SetPermissionMode(config::PermissionMode mode)
{
    permissionMode = mode;
    
    // 重建权限门控，保留已有的回调
    permissionGate = std::make_unique<permission::PermissionGate>(
        mode,
        approvalCallback
    );
}
```

### 5.2 设置审批回调

```cpp
void Agent::SetApprovalCallback(permission::ApprovalCallback callback)
{
    approvalCallback = std::move(callback);
    
    // 如果已有权限门控，更新它的回调
    if (permissionGate) {
        permissionGate = std::make_unique<permission::PermissionGate>(
            *permissionMode,
            approvalCallback
        );
    }
}
```

### 5.3 使用示例

```cpp
// 创建 Agent
Agent agent(provider, host, &toolManager);

// 设置权限模式为 Manual
agent.SetPermissionMode(PermissionMode::Manual);

// 设置审批回调
agent.SetApprovalCallback([](std::string_view toolName, const json& args, std::string_view description) {
    std::cout << "Tool: " << toolName << "\n";
    std::cout << "Description: " << description << "\n";
    std::cout << "Allow? (y/n): ";
    
    char c;
    std::cin >> c;
    
    return c == 'y' ? PermissionDecision::Allow : PermissionDecision::Deny;
});

// 现在 Prompt 会在执行危险工具时询问用户
auto result = agent.Prompt("帮我删除临时文件");
```

## 6. 记录集成

### 6.1 SetRecords

```cpp
void Agent::SetRecords(records::AgentRecords* records)
{
    this->records = records;
}
```

### 6.2 Resume

```cpp
absl::Status Agent::Resume()
{
    if (!records) {
        return absl::FailedPreconditionError("No records set");
    }

    // 读取所有记录
    auto allRecords = records->ReadAll();
    if (!allRecords.ok()) {
        return allRecords.status();
    }

    // 重放记录，恢复状态
    for (const auto& record : *allRecords) {
        // 恢复历史消息
        if (record.type == RecordType::ContextAppendMessage) {
            auto msg = ParseMessage(record.data["message"]);
            history.push_back(msg);
        }
        // 其他记录类型...
    }

    return absl::OkStatus();
}
```

## 7. Context 集成

### 7.1 为什么使用 ContextMemory

**问题**：裸 `vector<Message>` 无法追踪 Token 用量

```cpp
// ❌ 旧方式
std::vector<llm::Message> history;
// 每次检查 Token 需要重新计算
int64_t tokens = EstimateTokens(history);  // O(n)

// ✅ 新方式
context::ContextMemory history;
// Token 缓存，O(1) 查询
int64_t tokens = history.TokenCount();
```

### 7.2 压缩触发时机

```cpp
absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
{
    // ... 前置检查 ...
    
    // ===== Context 压缩检查 =====
    // 获取模型的 context window 大小
    auto capability = llm::GetCapability(provider->ModelName());
    int64_t maxTokens = compactionConfig 
        ? compactionConfig->maxContextTokens 
        : capability.maxContextTokens;
    
    // 计算当前用量 + 新消息
    int64_t usedTokens = history.TokenCount() + EstimateTokens(text);
    
    // 检查是否需要压缩
    context::CompactionConfig cfg{
        .maxContextTokens = maxTokens,
        .compactThreshold = 0.75,  // 75% 触发
        .retainTail = 10           // 保留最后 10 条
    };
    
    if (context::ShouldCompact(usedTokens, cfg)) {
        // 发送压缩开始事件
        Dispatch(ContextCompactingEvent{});
        
        // 执行压缩
        auto compactResult = context::Compact(provider, history.Messages(), cfg);
        if (compactResult.ok() && *compactResult) {
            // 用压缩后的历史替换
            auto newHistory = context::BuildCompactedHistory(
                (*compactResult)->summary,
                history.Messages(),
                cfg.retainTail
            );
            history.ReplaceAll(std::move(newHistory));
        }
        
        // 发送压缩结束事件
        Dispatch(ContextCompactedEvent{});
    }
    
    // 继续正常流程...
}
```

### 7.3 SetCompactionConfig

```cpp
void Agent::SetCompactionConfig(context::CompactionConfig cfg)
{
    compactionConfig = cfg;
}
```

**使用示例**：
```cpp
Agent agent(provider, host, &toolManager);

// 自定义压缩配置
agent.SetCompactionConfig({
    .maxContextTokens = 128000,  // GPT-4 的 context window
    .compactThreshold = 0.7,     // 70% 触发
    .retainTail = 15             // 保留最后 15 条消息
});
```

## 8. Hooks 集成

### 8.1 SetHookEngine

```cpp
void Agent::SetHookEngine(hooks::HookEngine* engine)
{
    hookEngine = engine;
}
```

### 8.2 钩子触发点

Agent 触发 3 个钩子事件：

| 事件 | 类型 | 触发时机 |
|------|------|----------|
| `UserPromptSubmit` | 阻塞型 | 用户输入后，Prompt 执行前 |
| `PreCompact` | 信息型 | Context 压缩前 |
| `PostCompact` | 信息型 | Context 压缩后 |

```cpp
absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
{
    // ===== UserPromptSubmit 钩子 =====
    if (hookEngine) {
        auto block = hookEngine->TriggerBlock(
            hooks::HookEvent::UserPromptSubmit,
            {.event = UserPromptSubmit, .target = "", .payload = {{"prompt", text}}},
            {}
        );
        if (block && block->action == hooks::HookAction::Block) {
            return absl::CancelledError(block->reason);
        }
    }
    
    // 继续执行...
}
```

### 8.3 使用示例

```cpp
// 从配置创建 HookEngine
std::vector<hooks::HookDef> hookDefs = {
    {
        .event = hooks::HookEvent::UserPromptSubmit,
        .command = "/usr/local/bin/check-prompt.sh",
        .timeoutSeconds = 5
    }
};

hooks::HookEngine hookEngine(hookDefs, host);

// 设置到 Agent
agent.SetHookEngine(&hookEngine);
```

## 9. 测试分析

### 7.1 基本测试

```cpp
TEST(Agent, BasicPrompt) {
    MockChatProvider provider;
    provider.responses = {
        {.text = "Hello!", .finishReason = llm::FinishReason::Completed}
    };
    
    Agent agent(&provider);
    
    auto result = agent.Prompt("Hi");
    
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result->stopReason, StopReason::Completed);
    EXPECT_EQ(agent.GetHistory().size(), 2);  // User + Assistant
}

TEST(Agent, StatusTransitions) {
    MockChatProvider provider;
    provider.responses.resize(1);  // 延迟响应
    
    Agent agent(&provider);
    EXPECT_EQ(agent.GetStatus(), AgentStatus::Idle);
    
    // 在另一个线程执行
    std::thread([&]() {
        agent.Prompt("Hi");
    }).detach();
    
    // 等待开始
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(agent.GetStatus(), AgentStatus::Running);
    
    // 等待完成
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(agent.GetStatus(), AgentStatus::Idle);
}

TEST(Agent, Cancel) {
    MockChatProvider provider;
    provider.responses.resize(100);  // 很多响应
    
    Agent agent(&provider);
    
    std::thread([&]() {
        std::this_thread::sleep_for(50ms);
        agent.Cancel();
    }).detach();
    
    auto result = agent.Prompt("Hi");
    
    EXPECT_EQ(result->stopReason, StopReason::Aborted);
}
```

### 7.2 权限测试

```cpp
TEST(Agent, PermissionGate) {
    MockChatProvider provider;
    provider.responses = {
        {
            .toolCalls = {llm::ToolCall{"1", "Bash", "{\"command\":\"rm -rf /\"}"}},
            .finishReason = llm::FinishReason::ToolCalls
        },
        {.text = "Done", .finishReason = llm::FinishReason::Completed}
    };
    
    MockTool bashTool;
    bashTool.Name = "Bash";
    bashTool.resolveHandler = [](auto) {
        return ToolExecution{.description = "rm -rf /", .requiresPermission = true};
    };
    
    ToolManager tools;
    tools.Register(std::make_unique<MockTool>(bashTool));
    
    Agent agent(&provider, nullptr, &tools);
    
    // 设置权限回调，自动拒绝
    int callbackCount = 0;
    agent.SetPermissionMode(PermissionMode::Manual);
    agent.SetApprovalCallback([&](auto, auto, auto) {
        callbackCount++;
        return PermissionDecision::Deny;
    });
    
    auto result = agent.Prompt("删除所有文件");
    
    EXPECT_EQ(callbackCount, 1);  // 回调被调用
    // 工具被拒绝，但没有工具结果，可能出错
}
```

## 8. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                           Agent                                      │
│                       (组合根)                                        │
├─────────────────────────────────────────────────────────────────────┤
│  外部依赖（非拥有）:                                                  │
│  - provider: ChatProvider*                                           │
│  - host: Host*                                                       │
│  - toolManager: ToolManager*                                         │
│  - records: AgentRecords*                                            │
│                                                                      │
│  内部状态:                                                           │
│  - history: vector<Message>                                          │
│  - activeTools: vector<string>                                       │
│  - status: AgentStatus                                               │
│  - permissionGate: unique_ptr<PermissionGate>                        │
│  - currentStopSource: optional<stop_source>                          │
│                                                                      │
│  方法:                                                               │
│  + Prompt(text) → StatusOr<PromptResult>                            │
│  + Cancel()                                                          │
│  + ClearContext()                                                    │
│  + SetPermissionMode(mode)                                           │
│  + SetApprovalCallback(callback)                                     │
│  + SetRecords(records)                                               │
│  + Resume() → Status                                                 │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    │ 协调
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        RunTurn(input)                                │
│                                                                      │
│  input.provider ← provider                                           │
│  input.tools ← BuildLoopTools()                                      │
│  input.host ← host                                                   │
│  input.history ← history                                             │
│  input.permissionGate ← permissionGate                               │
│  input.stopToken ← currentStopSource                                 │
└─────────────────────────────────────────────────────────────────────┘
```

## 9. 与其他模块的关系

```
Agent 作为组合根，协调所有模块：

依赖：
  Agent → ChatProvider (LLM 调用)
  Agent → ToolManager (工具管理)
  Agent → Host (文件/进程)
  Agent → PermissionGate (权限控制)
  Agent → AgentRecords (事件记录)
  Agent → Loop (执行循环)

被使用：
  Session → Agent (会话中的 Agent 实例)
  CLI/TUI → Agent (用户接口)
```

## 10. 小结

本章我们学习了：

- **Agent 的角色**：组合根，协调所有子系统
- **核心方法**：Prompt、Cancel、SetPermissionMode
- **类型定义**：AgentStatus、AgentConfig、PromptResult、AgentEvent
- **权限集成**：通过 PermissionGate 控制
- **记录集成**：通过 AgentRecords 持久化

## 11. 练习建议

1. **阅读源码**：打开 `Agent/Agent.cpp`，跟踪一次 Prompt 调用
2. **写测试**：测试状态转换（Idle → Running → Idle）
3. **思考题**：为什么 PermissionGate 是 unique_ptr 而不是直接持有？

## 12. 下一步

下一章我们将深入 **权限系统**，理解安全门控机制。

→ [07-permission-system.md](07-permission-system.md)