# CodeHarness

## 项目描述

CodeHarness 是 [OpenHarness](https://github.com/HKUDS/OpenHarness)(docs目录包含详细设计文档) 的 C++20 重写实现。OpenHarness 是一个 agent harness，负责将 LLM 封装为可以安全工作的 agent——提供模型客户端、上下文拼装、工具系统、权限系统、记忆、UI 和会话管理。

核心运行链路：

```
用户输入
  -> CLI / TUI
  -> RuntimeBundle
  -> QueryEngine.submit_message
  -> run_query agent loop
  -> API client stream_message
  -> 模型返回文本 delta 或 tool_use
  -> ToolRegistry 查找工具
  -> Hooks + PermissionChecker
  -> tool.execute
  -> ToolResultBlock 回填为 user message
  -> 下一轮模型继续
```

## 实施阶段

### 阶段 1：agent loop
- CLI：`codeharness -p "prompt"`
- Provider：OpenAI-compatible / Anthropic streaming
- Messages：`TextBlock`、`ToolUseBlock`、`ToolResultBlock`
- Tools：`read_file`、`glob`、`grep`
- Engine：工具调用回填和 max turns
- Session：JSON snapshot

### 阶段 2：安全工具执行
- `bash`、`write_file`、`edit_file`
- `PermissionChecker`、敏感路径硬拒绝、默认模式确认
- 工具输出截断和 artifact 保存

### 阶段 3：上下文系统
- system prompt builder、CLAUDE.md discovery
- skills loader、memory store、slash commands

### 阶段 4：MCP 和插件
- MCP stdio/http transport、JSON-RPC
- plugin manifest、skills、commands、MCP config

### 阶段 5：交互和多 agent
- JSON Lines backend-only 协议、TUI
- TaskManager、subprocess subagent
- Mailbox 和 team lifecycle

### 阶段 6：ohmo 和 gateway
- ohmo workspace、MessageBus、Channel adapter
- Gateway runtime pool

## C++ 编码规范

遵循 [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) 标准，具体要求：

### 基本原则
- **RAII everywhere**：资源生命周期绑定到对象生命周期
- **Immutability by default**：优先 `const`/`constexpr`，可变是例外
- **Type safety**：用类型系统在编译期阻止错误
- **Express intent**：名称、类型、概念应传达目的
- **Value semantics**：优先值语义而非指针语义

### 关键约定
- `.h` + `.cpp` 文件结构，`#pragma once`
- `namespace codeharness { ... }`，内部命名空间如 `codeharness::tools`
- `snake_case` 命名风格（函数、变量），`PascalCase` 类型名
- 类成员 `trailing_underscore_`
- `enum class` 而非 `enum`，不用 ALL_CAPS 枚举值
- 单参数构造函数必须 `explicit`
- `nullptr` 而非 `0`/`NULL`
- `{}` 初始化语法，避免窄化转换
- `'\n'` 而非 `std::endl`
- 基类析构函数：`public virtual` 或 `protected non-virtual`
- Rule of Zero / Rule of Five
- 模板参数用 concept 约束（C++20）
- 锁必须用 RAII（`scoped_lock`/`lock_guard`）
- 异常：throw by value, catch by reference，自定义异常类型

### 代码风格
- `.clang-format` 基于 Microsoft style，提交前格式化
- 禁止 C 风格 cast，用 `static_cast`/`dynamic_cast` 等
- 禁止裸 `new`/`delete`，用 `make_unique`/`make_shared`
- 函数保持简短单一职责

## 设计原则

1. **避免重复造轮子**：若第三方库已提供稳定功能（如 nlohmann_json 解析、asio 网络、reproc 进程管理、spdlog 日志），直接使用，不在项目内自研替代品。
2. **代码逻辑清晰直接**：不追求过度抽象。优先用简单的 `struct` + 自由函数，而非复杂的类层次。
3. **安全性适度**：权限系统必须在工具执行前做，敏感路径不能被 `full_auto` 绕过；但避免过于严格的安全性判断——不引入不必要的沙箱、不做过度的输入消毒、不做运行时类型检查之外的防御式编程。信任类型系统，信任已选的第三方库。
4. **统一消息模型**：所有 provider 转换成同一个内部消息模型，engine 不直接依赖特定 provider 格式。
5. **事件驱动**：engine 产出事件而非直接操作 UI，CLI、TUI、JSON 输出、测试消费同一套事件。
6. **工具失败不崩溃**：工具失败变成 `ToolResultBlock{is_error=true}` 回给模型。
