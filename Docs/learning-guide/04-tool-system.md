# 第4章：工具系统深度剖析

> 工具是 Agent 的"手脚"，定义它能执行的所有操作。

## 1. 为什么需要工具抽象？

### 1.1 Agent 能做什么？

LLM 本身只能生成文本，但 Agent 需要：
- 读取文件
- 编辑代码
- 执行命令
- 搜索文件
- 访问网络
- ...

**工具的作用**：将 LLM 的意图转化为实际操作。

### 1.2 两阶段执行模型

**核心设计**：工具执行分为两个阶段

```
阶段1: ResolveExecution(args) → ToolExecution
       - 纯验证，无副作用
       - 返回：将发生什么、是否需要权限
       
阶段2: Execute(args, ctx) → ToolResult
       - 实际执行，有副作用
       - 返回：执行结果
```

**为什么两阶段？**

1. **权限检查**：在执行前决定是否允许
2. **UI 预览**：告诉用户将发生什么
3. **取消机会**：用户可以在执行前取消
4. **测试友好**：可以单独测试验证逻辑

## 2. ExecutableTool 接口详解

### 2.1 接口定义

**位置**：`Source/CodeHarness/Engine/Tool.h`

```cpp
// 工具执行结果
struct ToolResult
{
    std::string content;   // 输出内容（成功或错误信息）
    bool isError = false;  // 是否为错误
};

// 工具执行上下文
struct ToolContext
{
    host::Host* host = nullptr;    // Host 接口，用于文件/进程操作
    std::stop_token stopToken;     // 取消令牌
};

// 工具执行信息（验证阶段返回）
struct ToolExecution
{
    std::string description;        // 描述将发生什么
    bool requiresPermission = false; // 是否需要权限检查
};

// 工具接口
class ExecutableTool
{
public:
    virtual ~ExecutableTool() = default;

    // ===== 元信息 =====
    
    // 工具名称（用于 LLM 调用）
    virtual std::string Name() const = 0;
    
    // 工具描述（LLM 会看到，用于决定何时调用）
    virtual std::string Description() const = 0;
    
    // 参数 JSON Schema
    virtual nlohmann::json Parameters() const = 0;

    // ===== 两阶段执行 =====
    
    // 阶段1：纯验证
    // 输入：工具参数
    // 输出：ToolExecution（描述 + 是否需要权限）
    // 注意：不能有副作用！
    virtual absl::StatusOr<ToolExecution> ResolveExecution(const nlohmann::json& args) = 0;
    
    // 阶段2：实际执行
    // 输入：工具参数 + 执行上下文
    // 输出：ToolResult（内容 + 是否错误）
    // 注意：可能有副作用（写文件、执行命令等）
    virtual absl::StatusOr<ToolResult> Execute(const nlohmann::json& args, const ToolContext& ctx) = 0;

    // ===== 辅助方法 =====
    
    // 生成 LLM 可见的工具定义
    llm::Tool GetToolDefinition() const
    {
        return {
            Name(), 
            Description(), 
            Parameters().is_null() ? nlohmann::json::object() : Parameters()
        };
    }
};
```

### 2.2 工具定义示例

**Bash 工具**：
```cpp
std::string BashTool::Name() const { return "Bash"; }

std::string BashTool::Description() const {
    return "Execute a shell command. "
           "Runs in the foreground with a timeout; "
           "on timeout or cancellation the process is terminated. "
           "Stdout and stderr are drained concurrently.";
}

nlohmann::json BashTool::Parameters() const {
    return {
        {"type", "object"},
        {"properties", {
            {"command", {
                {"type", "string"},
                {"description", "The shell command to execute"}
            }},
            {"timeout", {
                {"type", "integer"},
                {"description", "Timeout in milliseconds (default 60000, max 300000)"}
            }},
            {"cwd", {
                {"type", "string"},
                {"description", "Working directory for the command"}
            }}
        }},
        {"required", {"command"}}
    };
}
```

**LLM 看到的定义**：
```json
{
    "name": "Bash",
    "description": "Execute a shell command...",
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

## 3. 两阶段执行详解

### 3.1 阶段1：ResolveExecution

**职责**：
- 验证参数格式
- 检查前置条件（文件是否存在等）
- 生成执行描述
- 标记是否需要权限

**示例：WriteFile 工具**

```cpp
absl::StatusOr<ToolExecution> WriteFileTool::ResolveExecution(const nlohmann::json& args) {
    // 1. 验证参数
    if (!args.contains("path")) {
        return absl::InvalidArgumentError("missing 'path' argument");
    }
    if (!args.contains("content")) {
        return absl::InvalidArgumentError("missing 'content' argument");
    }
    
    std::string path = args["path"].get<std::string>();
    std::string content = args["content"].get<std::string>();
    
    // 2. 生成描述
    std::string description = fmt::format(
        "Write {} bytes to '{}'",
        content.size(),
        path
    );
    
    // 3. 写文件需要权限
    return ToolExecution{
        .description = description,
        .requiresPermission = true  // 写文件是危险操作
    };
}
```

**示例：ReadFile 工具**

```cpp
absl::StatusOr<ToolExecution> ReadFileTool::ResolveExecution(const nlohmann::json& args) {
    // 1. 验证参数
    if (!args.contains("path")) {
        return absl::InvalidArgumentError("missing 'path' argument");
    }
    
    std::string path = args["path"].get<std::string>();
    
    // 2. 生成描述
    std::string description = fmt::format("Read file '{}'", path);
    
    // 3. 读文件不需要权限
    return ToolExecution{
        .description = description,
        .requiresPermission = false  // 读文件是安全操作
    };
}
```

### 3.2 阶段2：Execute

**职责**：
- 执行实际操作
- 使用 Host 进行文件/进程操作
- 处理错误

**示例：ReadFile 工具**

```cpp
absl::StatusOr<ToolResult> ReadFileTool::Execute(
    const nlohmann::json& args,
    const ToolContext& ctx
) {
    std::string path = args["path"].get<std::string>();
    
    // 使用 Host 读取文件
    auto linesResult = ctx.host->ReadLines(path);
    if (!linesResult.ok()) {
        return ToolResult{
            .content = fmt::format("Failed to read '{}': {}", path, linesResult.status().message()),
            .isError = true
        };
    }
    
    // 添加行号
    std::string output;
    int lineNum = 1;
    for (const auto& line : *linesResult) {
        output += fmt::format("{}: {}\n", lineNum, line);
        lineNum++;
    }
    
    return ToolResult{
        .content = output,
        .isError = false
    };
}
```

### 3.3 Loop 中的两阶段执行

**位置**：`Source/CodeHarness/Engine/Loop.cpp:28-70`

```cpp
ToolResult ExecuteToolCall(
    ExecutableTool& tool,
    const llm::ToolCall& tc,
    const ToolContext& ctx,
    const TurnInput& input
) {
    // ===== 解析参数 =====
    nlohmann::json args;
    if (!tc.arguments.empty()) {
        try {
            args = nlohmann::json::parse(tc.arguments);
        } catch (const nlohmann::json::parse_error& e) {
            return {.content = fmt::format("invalid tool arguments: {}", e.what()), .isError = true};
        }
    }

    // ===== 阶段1：验证 =====
    auto resolution = tool.ResolveExecution(args);
    if (!resolution.ok()) {
        return {.content = std::string(resolution.status().message()), .isError = true};
    }

    const auto& exec = *resolution;
    
    // ===== 权限检查 =====
    // 如果工具需要权限，且有权限门控，则检查
    if (input.permissionGate != nullptr && exec.requiresPermission) {
        // 发送权限请求事件（通知 UI）
        Dispatch(input, PermissionRequestedEvent{tc.name, args, exec.description});
        
        // 询问权限门控
        if (!input.permissionGate->ShouldRun(true, tc.name, args, exec.description)) {
            // 权限被拒绝
            Dispatch(input, PermissionDeniedEvent{tc.name, exec.description});
            return {.content = fmt::format("permission denied for tool '{}'", tc.name), .isError = true};
        }
    }

    // ===== 阶段2：执行 =====
    auto result = tool.Execute(args, ctx);
    if (!result.ok()) {
        return {.content = std::string(result.status().message()), .isError = true};
    }

    return std::move(*result);
}
```

## 4. 内置工具详解

### 4.1 文件工具

| 工具 | 功能 | 需要权限 | 读写 |
|------|------|----------|------|
| Read | 读取文件内容 | 否 | 读 |
| Write | 写入文件 | 是 | 写 |
| Edit | 编辑文件（替换文本） | 是 | 写 |
| Glob | 搜索文件 | 否 | 读 |
| Grep | 搜索文件内容 | 否 | 读 |

**Read 工具**：
```yaml
name: Read
description: Read file contents with line numbers
parameters:
  path: string           # 文件路径
  line_offset?: int      # 起始行（1-based，负数从末尾算）
  n_lines?: int          # 最多读取行数
```

**Write 工具**：
```yaml
name: Write
description: Create or overwrite a file
parameters:
  path: string           # 文件路径
  content: string        # 写入内容
```

**Edit 工具**：
```yaml
name: Edit
description: Replace exact text in a file
parameters:
  file_path: string      # 文件路径
  old_string: string     # 要替换的文本
  new_string: string     # 新文本
  replace_all?: bool     # 是否替换所有出现
```

### 4.2 Shell 工具

**Bash 工具**：
```yaml
name: Bash
description: Execute a shell command
parameters:
  command: string        # 命令字符串
  timeout?: int          # 超时（毫秒，默认60000，最大300000）
  cwd?: string           # 工作目录
```

**安全特性**：
- 超时自动终止
- 两阶段终止（SIGTERM → SIGKILL）
- 输出截断（最大 50000 字符）
- 支持取消

### 4.3 工具输出截断

**位置**：`Source/CodeHarness/Tools/ToolOutput.h`

```cpp
// 截断过长的输出
std::string TruncateOutput(std::string_view output, size_t maxLen = 50000) {
    if (output.size() <= maxLen) {
        return std::string(output);
    }
    
    // 保留前半部分和后半部分
    size_t half = maxLen / 2;
    return fmt::format(
        "{}\n\n... [truncated {} bytes] ...\n\n{}",
        output.substr(0, half),
        output.size() - maxLen,
        output.substr(output.size() - half)
    );
}
```

## 5. ToolManager

### 5.1 类定义

```cpp
// 工具管理器：拥有工具实例，提供查找和遍历
class ToolManager
{
public:
    ToolManager() = default;
    ~ToolManager() = default;
    ToolManager(const ToolManager&) = delete;
    ToolManager& operator=(const ToolManager&) = delete;

    // 注册工具（转移所有权）
    void Register(std::unique_ptr<engine::ExecutableTool> tool);

    // 按名称查找
    engine::ExecutableTool* Find(std::string_view name) const;

    // 获取所有工具指针（用于 TurnInput.tools）
    std::vector<engine::ExecutableTool*> LoopTools() const;

    // 工具数量
    std::size_t Size() const;

private:
    std::vector<std::unique_ptr<engine::ExecutableTool>> tools;
};
```

### 5.2 使用示例

```cpp
// 创建工具管理器
ToolManager manager;

// 注册内置工具
manager.Register(std::make_unique<BashTool>());
manager.Register(std::make_unique<ReadFileTool>());
manager.Register(std::make_unique<WriteFileTool>());

// 查找工具
ExecutableTool* bash = manager.Find("Bash");
if (bash) {
    auto exec = bash->ResolveExecution({{"command", "ls"}});
    // ...
}

// 获取所有工具（传给 Loop）
std::vector<ExecutableTool*> tools = manager.LoopTools();
TurnInput input;
input.tools = tools;
```

## 6. 实现一个新工具

### 6.1 步骤

1. 创建类，继承 `ExecutableTool`
2. 实现 `Name()`、`Description()`、`Parameters()`
3. 实现 `ResolveExecution()`（验证）
4. 实现 `Execute()`（执行）
5. 注册到 `ToolManager`

### 6.2 示例：实现一个简单工具

```cpp
// EchoTool.h
#pragma once

#include "Engine/Tool.h"

namespace codeharness::tools
{

// Echo 工具：简单返回输入（用于测试）
class EchoTool : public engine::ExecutableTool
{
public:
    std::string Name() const override { return "Echo"; }
    
    std::string Description() const override {
        return "Echo back the input message. Useful for testing.";
    }
    
    nlohmann::json Parameters() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"message", {
                    {"type", "string"},
                    {"description", "The message to echo"}
                }}
            }},
            {"required", {"message"}}
        };
    }
    
    absl::StatusOr<engine::ToolExecution> ResolveExecution(
        const nlohmann::json& args
    ) override {
        // 验证参数
        if (!args.contains("message")) {
            return absl::InvalidArgumentError("missing 'message'");
        }
        
        std::string message = args["message"].get<std::string>();
        
        // 生成描述
        return ToolExecution{
            .description = fmt::format("Echo: '{}'", message),
            .requiresPermission = false  // Echo 不需要权限
        };
    }
    
    absl::StatusOr<engine::ToolResult> Execute(
        const nlohmann::json& args,
        const engine::ToolContext& ctx
    ) override {
        // 执行：直接返回消息
        std::string message = args["message"].get<std::string>();
        
        return ToolResult{
            .content = message,
            .isError = false
        };
    }
};

} // namespace codeharness::tools
```

### 6.3 注册使用

```cpp
// 在 Agent 初始化时注册
ToolManager manager;
manager.Register(std::make_unique<EchoTool>());

// 或者手动添加
Agent agent(provider, host, &manager);
```

## 7. 测试分析

### 7.1 工具测试模式

```cpp
// 使用 MockHost 测试工具
class MockHost : public host::Host {
    // Mock 方法...
};

TEST(ReadFileTool, BasicRead) {
    // 创建 Mock
    MockHost mock;
    EXPECT_CALL(mock, ReadLines("test.txt"))
        .WillOnce(Return(std::vector<std::string>{"line1", "line2"}));
    
    // 创建工具
    ReadFileTool tool;
    
    // 验证
    auto resolution = tool.ResolveExecution({{"path", "test.txt"}});
    EXPECT_TRUE(resolution.ok());
    EXPECT_FALSE(resolution->requiresPermission);
    
    // 执行
    ToolContext ctx{.host = &mock};
    auto result = tool.Execute({{"path", "test.txt"}}, ctx);
    
    EXPECT_FALSE(result->isError);
    EXPECT_EQ(result->content, "1: line1\n2: line2\n");
}
```

### 7.2 BashTool 测试

```cpp
TEST(BashTool, ExecuteEcho) {
    // 使用真实 LocalHost（或 Mock）
    LocalHost host;
    
    BashTool tool;
    auto resolution = tool.ResolveExecution({{"command", "echo hello"}});
    EXPECT_TRUE(resolution.ok());
    EXPECT_TRUE(resolution->requiresPermission);  // Bash 需要权限
    
    ToolContext ctx{.host = &host};
    auto result = tool.Execute({{"command", "echo hello"}}, ctx);
    
    EXPECT_FALSE(result->isError);
    EXPECT_EQ(result->content, "hello\n");
}

TEST(BashTool, Timeout) {
    LocalHost host;
    BashTool tool;
    
    ToolContext ctx{.host = &host};
    auto result = tool.Execute({
        {"command", "sleep 10"},
        {"timeout", 1000}  // 1秒超时
    }, ctx);
    
    EXPECT_TRUE(result->isError);
    EXPECT_TRUE(result->content.find("timeout") != std::string::npos);
}
```

## 8. 类关系图

```
┌─────────────────────────────────────────────────────────────┐
│                     ExecutableTool                          │
│                     (interface)                             │
├─────────────────────────────────────────────────────────────┤
│  + Name() → string                                          │
│  + Description() → string                                   │
│  + Parameters() → json                                      │
│  + ResolveExecution(args) → StatusOr<ToolExecution>         │
│  + Execute(args, ctx) → StatusOr<ToolResult>                │
│  + GetToolDefinition() → llm::Tool                          │
└─────────────────────────┬───────────────────────────────────┘
                          │
          ┌───────────────┼───────────────┐
          │               │               │
          ▼               ▼               ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│  BashTool   │ │ ReadFileTool│ │ WriteFileTool│
├─────────────┤ ├─────────────┤ ├─────────────┤
│ - host      │ │             │ │             │
│             │ │             │ │             │
│ Resolve     │ │ Resolve     │ │ Resolve     │
│ Execute     │ │ Execute     │ │ Execute     │
└─────────────┘ └─────────────┘ └─────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      ToolManager                            │
├─────────────────────────────────────────────────────────────┤
│  - tools: vector<unique_ptr<ExecutableTool>>               │
│  + Register(tool)                                          │
│  + Find(name) → ExecutableTool*                            │
│  + LoopTools() → vector<ExecutableTool*>                   │
└─────────────────────────────────────────────────────────────┘
```

## 9. 与其他模块的关系

```
工具系统与其他模块的关系：

被使用：
  Loop → ExecuteToolCall() 调用工具的两阶段方法
  Agent → 持有 ToolManager，管理工具生命周期

依赖：
  Tools → Host（文件/进程操作）
  Tools → Engine::Tool（接口定义）
```

## 10. 小结

本章我们学习了：

- **为什么需要工具抽象**：将 LLM 意图转化为操作
- **两阶段执行模型**：先验证，再执行
- **ExecutableTool 接口**：Name、Description、Parameters、ResolveExecution、Execute
- **内置工具**：文件工具、Shell 工具
- **ToolManager**：注册、查找、遍历
- **如何实现新工具**：继承接口，实现五个方法

## 11. 练习建议

1. **阅读源码**：打开 `Tools/Bash.cpp`，看完整实现
2. **实现工具**：尝试实现一个简单的工具，如 `EchoTool` 或 `TimeTool`
3. **思考题**：为什么 Bash 工具需要权限而 Read 不需要？还有哪些操作应该需要权限？

## 12. 下一步

下一章我们将深入 **Loop 引擎**，理解 Agent 的核心执行循环。

→ [05-loop-engine.md](05-loop-engine.md)