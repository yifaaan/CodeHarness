# engine/ — 智能体核心循环模块

## 设计目标

实现 LLM agent 的核心"思考-行动-观察"循环。这是 CodeHarness 的中枢神经系统。

## 架构

```
run_streaming(request, event_sink)
  └─ while turn < max_turns:
       ├─ provider.stream(messages)          ← 调用 LLM
       │    └─ emit ProviderEvent 到 event_sink
       ├─ collect_tool_uses()                ← 提取工具调用
       ├─ for each tool_use:
       │    ├─ permission_check              ← 权限校验
       │    ├─ tool.execute(context)         ← 执行工具
       │    └─ backfill ToolResultBlock       ← 结果回填
       └─ messages.append(tool_result)
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `Engine` | 核心类，持有 provider 和 tool_registry |
| `RunRequest` | 输入：prompt、system_prompt、options（max_turns 等） |
| `RunResult` | 输出：messages、output_text |
| `EngineEvent` | 事件（AssistantTextDelta、ToolStarted、ToolFinished、ToolResult、Error） |

### 两种模式

- **`run()`** — 阻塞式，适合 CLI 使用
- **`run_streaming(event_sink)`** — 事件驱动，适合 TUI / UI Backend

## 设计要点

- 每次 tool call 后，`ToolResultBlock` 以 `role: user` 消息回填到上下文中（类似 Anthropic 的 tool use 协议）
- `max_turns` 硬限制防止无限循环（默认 10 轮）
- Engine 不直接操作 UI，而是发射事件由外部消费

## 初学者指南

- 这是理解 agent 如何工作的最佳起点
- 阅读顺序：先看 `engine.h` 了解接口，再看 `engine.cpp` 跟踪循环
- 核心路径：`Engine::run_streaming()` → provider 流式响应 → 检测 `tool_use` → 执行工具 → 结果回填 → 下一轮
