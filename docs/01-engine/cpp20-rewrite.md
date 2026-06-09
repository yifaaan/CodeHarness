# Engine C++20 实现参考

Engine 模块的 C++20 实现已完成，代码见 `src/codeharness/engine/engine.h`（159 行声明）+ `engine.cpp`（448 行实现）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| 管理对话历史 | `Engine::messages()` / `Engine::load_messages()` |
| 调用统一 Provider 接口 | `m_api_client` 持有 `Provider` 抽象 |
| 消费 streaming event | `run_streaming()` 内 lambda 回调 |
| 执行 tool calls | `execute_tool_use()` — 查找 → 权限 → hook → 执行 → 回填 |
| tool results 回填给模型 | 追加 `ToolResultBlock` 作为下一轮 user message |
| 输出统一事件给 CLI/TUI | `EngineEvent` variant：`TextDelta`、`ToolUse`、`ToolResult`、`Status`、`Error`、`Done` |
| 错误、权限拒绝、max turns | 三者在 `submit_message()`/`run_streaming()` 中分别处理 |

消息模型和事件类型分别定义于 `core/message.h` 和 `engine/engine.h`。

## 核心接口

```
Engine                           主机 loop，持有 provider/registry/permission/hooks
├── submit_message(text, emit)   用户文本消息入口
├── run_streaming(request, emit) 核心 agent loop（最大 200 轮）
├── execute_tool_use(call, emit) 单个 tool use 的完整生命周期
└── messages() / load_messages   对话历史访问
```

Tool 执行完整流程（`engine.cpp` `execute_tool_use()`）：查找工具 → 权限目标提取 → `PermissionChecker.evaluate()` → PreToolUse hook → `Tool::execute()` → 输出截断 → PostToolUse hook → `ToolResponse`。

## 消息模型（`core/message.h`）

```
Message           role + content blocks
  Role::User | Assistant | Tool | System
ContentBlock      std::variant<TextBlock, ImageBlock, ToolUseBlock, ToolResultBlock>
```

## 事件模型（`engine/engine.h`）

```
EngineEvent       std::variant<
                    TextDelta, ToolUseDelta, ToolUse,
                    ToolResult, StatusEvent, ErrorEvent, DoneEvent
                  >
```

`submit_message()` 阻塞执行但通过 `emit` 回调实时输出事件，CLI/TUI/Tests 统一消费。

## 整体架构设计参考

### Provider 抽象

Engine 只依赖 `Provider` 接口（`provider/provider.h`）：

- `Provider::stream_message(Request, on_event)` — callback 驱动
- 子类：`OpenAIProvider`、`AnthropicProvider`、`EchoProvider`（测试用）

### Tool 抽象

Engine 通过 `ToolRegistry` 访问工具：

- `Tool::execute(json args, ToolContext) → ToolResponse`
- `Tool::input_schema()` / `is_read_only()` / `permission_target()` / `name()` / `description()`

注册在 `runtime/runtime.cpp` 的 `create_tool_registry()` 中。

### Permission 集成

`PermissionChecker::evaluate()` 在工具执行前调用。支持三种模式：
- `Default` — 只读自动允许，写操作需确认
- `Plan` — 阻止所有写操作
- `FullAuto` — 自动允许，但敏感路径仍硬拒绝

## 当前引擎暂未实现

以下功能暂不在当前 C++ 实现范围内：

- **并发工具执行**：当前顺序执行多个 tool use
- **Auto compact**：上下文长度达到阈值时自动压缩历史
- **Cost tracker**：未独立统计
- **图像预处理**：`ImageBlock` 消息类型预留了接口，provider 尚未实现
