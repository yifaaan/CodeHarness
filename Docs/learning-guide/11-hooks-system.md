# 第11章：Hooks 模块详解

> Hooks 模块提供生命周期扩展点，让用户可以在特定事件发生时执行自定义脚本。

## 1. 为什么需要 Hooks？

### 1.1 扩展点需求

**用户可能想要**：
- **安全审计**：记录每次工具调用
- **输入验证**：阻止危险命令
- **自定义策略**：根据规则自动决定权限
- **通知集成**：发送 Webhook 通知

### 1.2 没有 Hooks 的问题

```cpp
// ❌ 问题：每次需要新扩展都要改核心代码
class Loop {
    void executeTool(tool, args) {
        // 硬编码的检查
        if (args["command"].contains("rm -rf")) {
            return Error("dangerous command");
        }
        // 硬编码的日志
        auditLog << "Tool: " << tool.Name();
        // ...更多硬编码
        tool.Execute(args);
    }
};

// 问题：
// 1. 核心代码越来越臃肿
// 2. 无法在不修改代码的情况下添加新功能
// 3. 不同部署环境需要不同的扩展
```

### 1.3 Hooks 的解决方案

```cpp
// ✅ 好：通过配置扩展
// config.toml
[[hooks]]
event = "PreToolUse"
command = "/usr/local/bin/check-dangerous.sh"
matcher = "Bash"  # 只检查 Bash 工具

[[hooks]]
event = "PostToolUse"
command = "/usr/local/bin/audit-log.sh"

// Agent 运行时自动调用这些脚本
```

**好处**：
1. **无代码扩展**：通过配置文件添加功能
2. **可插拔**：不同环境可以有不同钩子
3. **安全隔离**：钩子失败不影响主流程

## 2. HookEvent 事件类型

### 2.1 事件枚举

**位置**：`Source/CodeHarness/Hooks/HookTypes.h`

```cpp
// 钩子可以订阅的生命周期事件
// 11 种事件（13 种中的 11 种，SubagentStart/Stop 延迟到子 Agent 实现）
enum class HookEvent
{
    // ===== 阻塞型事件 =====
    // 可以阻止操作执行
    
    PreToolUse,          // 工具执行前（阻塞）
    UserPromptSubmit,    // 用户输入后，Prompt 执行前（阻塞）
    
    // ===== 信息型事件 =====
    // 不阻塞，只是通知
    
    PostToolUse,         // 工具成功执行后
    PostToolUseFailure,  // 工具执行失败后
    Stop,                // Agent 正常完成 Turn
    StopFailure,         // Agent Turn 异常结束
    SessionStart,        // 会话创建/恢复
    SessionEnd,          // 会话关闭
    PreCompact,          // Context 压缩前
    PostCompact,         // Context 压缩后
    Notification,        // 通用通知事件
};
```

### 2.2 阻塞型 vs 信息型

| 类型 | 事件 | 可以阻止？ | 触发点 |
|------|------|-----------|--------|
| **阻塞型** | `PreToolUse` | ✓ | 工具执行前 |
| **阻塞型** | `UserPromptSubmit` | ✓ | 用户输入后 |
| 信息型 | `PostToolUse` | ✗ | 工具成功后 |
| 信息型 | `PostToolUseFailure` | ✗ | 工具失败后 |
| 信息型 | `Stop` | ✗ | Turn 正常结束 |
| 信息型 | `StopFailure` | ✗ | Turn 异常结束 |
| 信息型 | `SessionStart` | ✗ | 会话开始 |
| 信息型 | `SessionEnd` | ✗ | 会话结束 |
| 信息型 | `PreCompact` | ✗ | 压缩前 |
| 信息型 | `PostCompact` | ✗ | 压缩后 |
| 信息型 | `Notification` | ✗ | 通用 |

### 2.3 事件触发点

```
用户输入
    │
    ▼
┌───────────────────────────────────────┐
│ UserPromptSubmit (阻塞)                │
│ 如果 Block → 返回错误                  │
└───────────────────────────────────────┘
    │
    ▼
Loop 执行
    │
    ▼
┌───────────────────────────────────────┐
│ PreToolUse (阻塞)                      │
│ 如果 Block → 跳过工具                  │
└───────────────────────────────────────┘
    │
    ▼
工具执行
    │
    ├──成功──► PostToolUse
    │
    └──失败──► PostToolUseFailure
    │
    ▼
Turn 结束
    │
    ├──正常──► Stop
    │
    └──异常──► StopFailure
```

## 3. HookDef / HookResult / HookContext

### 3.1 HookDef（钩子定义）

```cpp
// 一个 [[hooks]] 配置项
struct HookDef
{
    // 订阅的事件
    HookEvent event = HookEvent::Notification;
    
    // 要执行的命令
    std::string command;
    
    // 可选：正则匹配目标
    // - 工具事件：匹配工具名称
    // - Session 事件：匹配 session id
    // - 空 = 匹配所有
    std::optional<std::string> matcher;
    
    // 超时时间（秒）
    // 解析时被限制在 1..600 范围
    int timeoutSeconds = 30;
};
```

**配置示例**：

```toml
# 检查危险命令
[[hooks]]
event = "PreToolUse"
command = "/usr/local/bin/check-dangerous.sh"
matcher = "Bash"
timeout = 10

# 审计所有工具调用
[[hooks]]
event = "PostToolUse"
command = "/usr/local/bin/audit.sh"

# 阻止特定用户输入
[[hooks]]
event = "UserPromptSubmit"
command = "/usr/local/bin/check-prompt.sh"
```

### 3.2 HookResult（执行结果）

```cpp
// 单个钩子执行的结果
struct HookResult
{
    // 动作：Allow 或 Block
    // 默认 Allow（只有显式返回 Block 才阻塞）
    HookAction action = HookAction::Allow;
    
    // 原因（人类可读）
    std::string reason;
    
    // 捕获的 stdout
    std::string out;
    
    // 捕获的 stderr
    std::string err;
    
    // 退出码
    int exitCode = 0;
    
    // 是否失败（非零退出/超时/崩溃）
    // 根据 Fail-Open 不变量，失败 = Allow
    bool failed = false;
};
```

### 3.3 HookContext（执行上下文）

```cpp
// 单次钩子执行的上下文
struct HookContext
{
    // 触发的事件
    HookEvent event = HookEvent::Notification;
    
    // 事件目标
    // - PreToolUse/Post*: 工具名称
    // - SessionStart/End: session id
    // - UserPromptSubmit: 空
    std::string target;
    
    // 事件相关的数据（序列化为 JSON 输入到 stdin）
    nlohmann::json payload;
};
```

**stdin JSON 载荷示例**：

```json
// PreToolUse
{
    "event": "PreToolUse",
    "target": "Bash",
    "args": {"command": "ls -la"},
    "description": "Execute: ls -la"
}

// UserPromptSubmit
{
    "event": "UserPromptSubmit",
    "prompt": "帮我删除所有文件"
}

// PostToolUse
{
    "event": "PostToolUse",
    "target": "Bash",
    "result": {"content": "...", "isError": false}
}
```

### 3.4 HookAction

```cpp
// 钩子返回的动作
enum class HookAction
{
    Allow,  // 允许继续
    Block,  // 阻止操作
};
```

## 4. HookEngine 详解

### 4.1 类定义

**位置**：`Source/CodeHarness/Hooks/HookEngine.h`

```cpp
// HookEngine 运行用户配置的子进程钩子
//
// Fail-Open 不变量（架构不变量 #2）：
// 钩子失败（非零退出、超时、崩溃）总是被视为 Allow
// 阻止的唯一方式是在 stdout 输出 JSON {"action":"block","reason":"..."}
// 且只对 PreToolUse 和 UserPromptSubmit 有效
// 损坏的脚本永远不会阻塞 Agent
class HookEngine
{
public:
    // 构造：钩子列表 + Host（用于执行命令）
    HookEngine(std::vector<HookDef> hooks, host::Host* host);
    
    // ===== 信息型事件触发 =====
    
    // 最佳努力广播：运行所有订阅此事件的钩子
    // 用于 9 种信息型事件
    // 永远不阻塞调用者的控制流
    std::vector<HookResult> Trigger(
        HookEvent event,
        const HookContext& ctx,
        std::stop_token stopToken = {}
    );
    
    // ===== 阻塞型事件触发 =====
    
    // 阻塞查询：按顺序运行匹配的钩子
    // 返回第一个 Block 结果，或 nullopt（全部 Allow）
    // 只用于 PreToolUse / UserPromptSubmit
    std::optional<HookResult> TriggerBlock(
        HookEvent event,
        const HookContext& ctx,
        std::stop_token stopToken = {}
    );
    
    // 是否有任何钩子
    bool Empty() const { return hooks.empty(); }

private:
    // 检查钩子是否匹配事件和目标
    bool Matches(const HookDef& hook, HookEvent event, std::string_view target) const;
    
    // 运行单个钩子
    // 启动进程，通过 stdin 传入 JSON，drain stdout/stderr
    // 解析 stdout 中的 Block 决策
    HookResult RunOne(
        const HookDef& hook,
        const HookContext& ctx,
        std::stop_token stopToken
    );

    std::vector<HookDef> hooks;
    host::Host* host;
};
```

### 4.2 Trigger 实现（信息型）

```cpp
std::vector<HookResult> HookEngine::Trigger(
    HookEvent event,
    const HookContext& ctx,
    std::stop_token stopToken
) {
    std::vector<HookResult> results;
    
    // 遍历所有钩子
    for (const auto& hook : hooks) {
        // 检查是否匹配
        if (!Matches(hook, event, ctx.target)) {
            continue;
        }
        
        // 运行钩子
        auto result = RunOne(hook, ctx, stopToken);
        results.push_back(std::move(result));
        
        // 信息型事件：不检查 Block，继续运行所有钩子
    }
    
    return results;
}
```

### 4.3 TriggerBlock 实现（阻塞型）

```cpp
std::optional<HookResult> HookEngine::TriggerBlock(
    HookEvent event,
    const HookContext& ctx,
    std::stop_token stopToken
) {
    // 遍历所有钩子
    for (const auto& hook : hooks) {
        // 检查是否匹配
        if (!Matches(hook, event, ctx.target)) {
            continue;
        }
        
        // 运行钩子
        auto result = RunOne(hook, ctx, stopToken);
        
        // 检查是否 Block
        if (result.action == HookAction::Block) {
            return result;  // 立即返回，不运行后续钩子
        }
        
        // Allow 或失败：继续下一个钩子
    }
    
    // 全部 Allow
    return std::nullopt;
}
```

### 4.4 RunOne 实现

```cpp
HookResult HookEngine::RunOne(
    const HookDef& hook,
    const HookContext& ctx,
    std::stop_token stopToken
) {
    HookResult result;
    result.action = HookAction::Allow;  // 默认 Allow
    
    // ===== 1. 构建 JSON 载荷 =====
    nlohmann::json payload;
    payload["event"] = HookEventName(ctx.event);
    payload["target"] = ctx.target;
    payload["payload"] = ctx.payload;
    std::string stdinData = payload.dump();
    
    // ===== 2. 计算超时 =====
    int timeoutMs = std::clamp(hook.timeoutSeconds, 1, 600) * 1000;
    
    // ===== 3. 启动进程 =====
    // 使用 Host::ExecWithEnv 避免 shell 注入
    auto procResult = host->ExecWithEnv(
        {hook.command},  // argv 风格
        "",              // cwd
        {}               // env
    );
    
    if (!procResult.ok()) {
        // 启动失败 → Fail-Open → Allow
        result.failed = true;
        result.reason = "Failed to start hook process";
        return result;
    }
    
    auto& proc = *procResult;
    
    // ===== 4. 写入 stdin =====
    auto writeStatus = proc->WriteStdin(stdinData);
    if (!writeStatus.ok()) {
        result.failed = true;
        result.reason = "Failed to write to stdin";
        proc->Kill("SIGTERM");
        return result;
    }
    
    proc->CloseStdin();
    
    // ===== 5. Drain 输出 =====
    auto drainResult = proc->Drain(timeoutMs, stopToken);
    if (!drainResult.ok()) {
        result.failed = true;
        result.reason = "Failed to drain process";
        proc->Kill("SIGTERM");
        return result;
    }
    
    result.out = drainResult->out;
    result.err = drainResult->err;
    result.exitCode = drainResult->exitCode;
    result.finished = drainResult->finished;
    
    // ===== 6. 检查退出码 =====
    if (!drainResult->finished || drainResult->exitCode != 0) {
        // 非零退出 → Fail-Open → Allow
        result.failed = true;
        result.reason = fmt::format("Hook exited with code {}", drainResult->exitCode);
        return result;
    }
    
    // ===== 7. 解析 stdout 中的 Block 决策 =====
    // 格式：一行 JSON {"action":"block","reason":"..."}
    std::string_view output(result.out);
    
    // 查找 JSON 行
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (output[i] == '{') {
            // 尝试解析
            try {
                auto json = nlohmann::json::parse(output.substr(i));
                if (json.contains("action") && json["action"] == "block") {
                    result.action = HookAction::Block;
                    if (json.contains("reason")) {
                        result.reason = json["reason"].get<std::string>();
                    }
                }
            } catch (...) {
                // 解析失败 → 忽略，保持 Allow
            }
            break;
        }
    }
    
    return result;
}
```

## 5. Fail-Open 不变量

### 5.1 什么是 Fail-Open？

**原则**：钩子失败 = Allow

```
钩子执行结果:
  - 成功，返回 Allow → Allow
  - 成功，返回 Block → Block（唯一能阻止的方式）
  - 失败（超时） → Allow
  - 失败（崩溃） → Allow
  - 失败（非零退出） → Allow
  - 启动失败 → Allow
```

### 5.2 为什么 Fail-Open？

**原因**：
1. **防止阻塞**：配置错误不应阻止 Agent 工作
2. **安全默认**：Agent 能完成任务比配置问题更重要
3. **调试友好**：失败不会隐藏，stdout/stderr 都记录

### 5.3 设计含义

```cpp
// 只有显式返回 Block 才阻止
if (result.action == HookAction::Block) {
    // 阻止操作
    return Block;
}

// 其他所有情况都 Allow
return Allow;
```

## 6. 配置格式 (config.toml)

### 6.1 基本格式

```toml
# 检查危险命令（阻塞型）
[[hooks]]
event = "PreToolUse"
command = "/usr/local/bin/check-dangerous.sh"
matcher = "Bash"  # 正则匹配工具名
timeout = 10

# 审计日志（信息型）
[[hooks]]
event = "PostToolUse"
command = "/usr/local/bin/audit.sh"
timeout = 5

# 用户输入检查（阻塞型）
[[hooks]]
event = "UserPromptSubmit"
command = "/usr/local/bin/check-prompt.sh"
```

### 6.2 钩子脚本示例

**check-dangerous.sh**：
```bash
#!/bin/bash

# 从 stdin 读取 JSON
read -r json

# 提取命令
command=$(echo "$json" | jq -r '.payload.args.command // empty')

# 检查危险命令
if [[ "$command" =~ rm\ -rf\ / ]] || [[ "$command" =~ :\{\ :} ]]; then
    echo '{"action":"block","reason":"Dangerous command detected"}'
    exit 0
fi

# 允许
echo '{"action":"allow"}'
exit 0
```

**audit.sh**：
```bash
#!/bin/bash

# 从 stdin 读取 JSON
read -r json

# 写入审计日志
echo "$(date -Iseconds) $json" >> /var/log/agent-audit.log

# 信息型事件，不返回 Block
exit 0
```

## 7. 事件触发点详解

### 7.1 Loop 中的 5 个事件

```cpp
// Loop.cpp

// 1. PreToolUse（阻塞型）
// 在 ExecuteToolCall 中，ResolveExecution 之后
if (hookEngine) {
    auto block = hookEngine->TriggerBlock(
        hooks::HookEvent::PreToolUse,
        {.event = PreToolUse, .target = tc.name, .payload = {...}},
        stopToken
    );
    if (block && block->action == HookAction::Block) {
        // 跳过执行
        return ToolResult{.content = block->reason, .isError = true};
    }
}

// 2. PostToolUse（信息型）
// 工具成功执行后
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::PostToolUse,
        {.event = PostToolUse, .target = tc.name, .payload = {...}},
        stopToken
    );
}

// 3. PostToolUseFailure（信息型）
// 工具执行失败后
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::PostToolUseFailure,
        {.event = PostToolUseFailure, .target = tc.name, .payload = {...}},
        stopToken
    );
}

// 4. Stop（信息型）
// Turn 正常结束
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::Stop,
        {.event = Stop, .target = "", .payload = {...}},
        stopToken
    );
}

// 5. StopFailure（信息型）
// Turn 异常结束
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::StopFailure,
        {.event = StopFailure, .target = "", .payload = {...}},
        stopToken
    );
}
```

### 7.2 Agent 中的 3 个事件

```cpp
// Agent.cpp

// 1. UserPromptSubmit（阻塞型）
// Prompt 开始前
if (hookEngine) {
    auto block = hookEngine->TriggerBlock(
        hooks::HookEvent::UserPromptSubmit,
        {.event = UserPromptSubmit, .target = "", .payload = {{"prompt", text}}},
        {}
    );
    if (block && block->action == HookAction::Block) {
        return absl::CancelledError(block->reason);
    }
}

// 2. PreCompact（信息型）
// Context 压缩前
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::PreCompact,
        {.event = PreCompact, .target = "", .payload = {...}},
        {}
    );
}

// 3. PostCompact（信息型）
// Context 压缩后
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::PostCompact,
        {.event = PostCompact, .target = "", .payload = {...}},
        {}
    );
}
```

### 7.3 Session 中的 2 个事件

```cpp
// Session.cpp

// 1. SessionStart（信息型）
// 会话创建/恢复时
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::SessionStart,
        {.event = SessionStart, .target = sessionId, .payload = {...}},
        {}
    );
}

// 2. SessionEnd（信息型）
// 会话关闭时
if (hookEngine) {
    hookEngine->Trigger(
        hooks::HookEvent::SessionEnd,
        {.event = SessionEnd, .target = sessionId, .payload = {...}},
        {}
    );
}
```

## 8. 测试分析

### 8.1 HookEngine 测试

```cpp
TEST(HookEngine, TriggerWithNoHooks) {
    HookEngine engine({}, nullptr);
    EXPECT_TRUE(engine.Empty());
    
    auto results = engine.Trigger(HookEvent::PostToolUse, {}, {});
    EXPECT_TRUE(results.empty());
}

TEST(HookEngine, TriggerBlockWithMatchingHook) {
    // 创建临时脚本
    std::string script = R"(
        #!/bin/bash
        echo '{"action":"block","reason":"test block"}'
    )";
    // ... 写入临时文件 ...
    
    HookDef hook{
        .event = HookEvent::PreToolUse,
        .command = scriptPath,
        .matcher = std::nullopt,
        .timeoutSeconds = 5
    };
    
    LocalHost host;
    HookEngine engine({hook}, &host);
    
    auto result = engine.TriggerBlock(
        HookEvent::PreToolUse,
        {.event = PreToolUse, .target = "Bash", .payload = {}},
        {}
    );
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->action, HookAction::Block);
    EXPECT_EQ(result->reason, "test block");
}

TEST(HookEngine, FailOpenOnNonZeroExit) {
    // 创建返回非零的脚本
    std::string script = R"(
        #!/bin/bash
        exit 1
    )";
    
    HookDef hook{
        .event = HookEvent::PreToolUse,
        .command = scriptPath,
        .matcher = std::nullopt,
        .timeoutSeconds = 5
    };
    
    LocalHost host;
    HookEngine engine({hook}, &host);
    
    auto result = engine.TriggerBlock(
        HookEvent::PreToolUse,
        {.event = PreToolUse, .target = "Bash", .payload = {}},
        {}
    );
    
    // Fail-Open：非零退出 = Allow
    EXPECT_FALSE(result.has_value());  // nullopt = Allow
}

TEST(HookEngine, MatcherFiltersHooks) {
    HookDef hook{
        .event = HookEvent::PreToolUse,
        .command = scriptPath,
        .matcher = "Bash",  // 只匹配 Bash
        .timeoutSeconds = 5
    };
    
    LocalHost host;
    HookEngine engine({hook}, &host);
    
    // 不匹配的工具
    auto result = engine.TriggerBlock(
        HookEvent::PreToolUse,
        {.event = PreToolUse, .target = "Read", .payload = {}},
        {}
    );
    
    EXPECT_FALSE(result.has_value());  // 匹配器不匹配，钩子未运行
}
```

## 9. 类关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HookEngine                                    │
├─────────────────────────────────────────────────────────────────────┤
│  - hooks: vector<HookDef>    // 钩子定义列表                         │
│  - host: Host*               // 用于执行命令                         │
│                                                                      │
│  + Trigger(event, ctx) → vector<HookResult>    // 信息型            │
│  + TriggerBlock(event, ctx) → optional<HookResult>  // 阻塞型       │
│  + Empty() → bool                                                │
│                                                                      │
│  - Matches(hook, event, target) → bool                          │
│  - RunOne(hook, ctx) → HookResult                                │
└─────────────────────────────────────────────────────────────────────┘
        │
        │ 持有
        ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         HookDef                                      │
├─────────────────────────────────────────────────────────────────────┤
│  - event: HookEvent          // 订阅的事件                          │
│  - command: string           // 要执行的命令                         │
│  - matcher: optional<string>  // 目标匹配正则                        │
│  - timeoutSeconds: int       // 超时时间                            │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        HookContext                                   │
├─────────────────────────────────────────────────────────────────────┤
│  - event: HookEvent          // 触发的事件                          │
│  - target: string            // 事件目标                            │
│  - payload: json             // 事件数据（传入 stdin）               │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                        HookResult                                    │
├─────────────────────────────────────────────────────────────────────┤
│  - action: HookAction        // Allow / Block                       │
│  - reason: string            // 原因                                │
│  - out: string               // stdout                              │
│  - err: string               // stderr                              │
│  - exitCode: int             // 退出码                              │
│  - failed: bool              // 是否失败                            │
└─────────────────────────────────────────────────────────────────────┘

集成关系：

Loop ────────────→ HookEngine (input.hookEngine)
  │                     │
  │ TriggerBlock        │ Trigger
  │ (PreToolUse)        │ (PostToolUse, Stop, ...)
  │                     │
Agent ────────────→ HookEngine
  │                     │
  │ TriggerBlock        │ Trigger
  │ (UserPromptSubmit)  │ (PreCompact, PostCompact)
  │                     │
Session ───────────→ HookEngine
                      │
                      │ Trigger
                      │ (SessionStart, SessionEnd)
```

## 10. 与其他模块的关系

```
Hooks 模块的位置：

被使用：
  Loop → HookEngine (工具事件)
  Agent → HookEngine (用户输入/压缩事件)
  Session → HookEngine (会话事件)

依赖：
  Hooks → Host (执行命令)
  Hooks → nlohmann::json (stdin/stdout 格式)

关键点：
  - HookEngine 是可选的（null = 无钩子）
  - Fail-Open 保证钩子失败不影响主流程
  - 阻塞型只有 2 个，信息型有 9 个
```

## 11. 小结

本章我们学习了：

- **为什么需要 Hooks**：扩展点、可插拔、安全隔离
- **HookEvent**：11 种事件，阻塞型 vs 信息型
- **HookDef / HookResult / HookContext**：定义、结果、上下文
- **HookEngine**：Trigger（信息型）、TriggerBlock（阻塞型）
- **Fail-Open 不变量**：钩子失败 = Allow
- **配置格式**：config.toml 中的 `[[hooks]]` 数组
- **事件触发点**：Loop 5个、Agent 3个、Session 2个

**关键设计点**：
1. **Fail-Open**：钩子永远不能阻塞 Agent
2. **子进程隔离**：钩子运行在独立进程中
3. **JSON 通信**：stdin 输入，stdout 输出
4. **正则匹配**：matcher 过滤触发条件

## 12. 练习建议

1. **写钩子脚本**：实现一个简单的审计日志钩子
2. **配置测试**：在 config.toml 中添加钩子，测试触发
3. **思考题**：为什么信息型事件不返回 Block 决策？

## 13. 下一步

恭喜你完成了整个学习指南！现在你应该对 CodeHarness 的所有模块有了深入理解。

**建议的下一步**：
1. 阅读源代码，对照文档加深理解
2. 尝试实现自己的钩子脚本
3. 参考测试代码，学习测试技巧

**参考文档**：
- [AGENTS.md](../../AGENTS.md) — 项目总览
- [docs/plan/re-build/](../plan/re-build/) — 详细设计文档
- [docs/coding-conventions.md](../coding-conventions.md) — 编码规范

祝你学习愉快！