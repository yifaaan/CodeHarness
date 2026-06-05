# ui_backend/ — JSON Lines 后端协议模块

## 设计目标

提供基于 stdin/stdout 的 JSON Lines 协议，作为独立 UI 前端（如 TUI、Web UI）与 CodeHarness 引擎之间的通信桥梁。

## 架构

```
BackendHost::run()
  └─ 协议循环：
       ├─ 读取 stdin 的 JSON Lines →
       │    ├─ FrontendRequest::SubmitLine(prompt)  ← 提交用户输入
       │    └─ FrontendRequest::Shutdown              ← 关闭
       └─ 写入 stdout 的 JSON Lines（前缀 "OHJSON:"） ← 事件推送
            ├─ BackendEvent::Ready
            ├─ BackendEvent::AssistantDelta
            ├─ BackendEvent::ToolStarted
            ├─ BackendEvent::ToolCompleted
            ├─ BackendEvent::ToolResult
            ├─ BackendEvent::LineComplete
            ├─ BackendEvent::Error
            └─ BackendEvent::Shutdown
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `FrontendRequest` | 前端→后端请求：`SubmitLine`、`Shutdown` |
| `BackendEvent` | 后端→前端事件（引擎事件 JSON 序列化） |
| `BackendHost` | 协议循环主机：stdin 读取 + 引擎事件订阅 + stdout 输出 |
| `BackendEventHandler` | 引擎事件到后端事件的转换 |

## 设计要点

- 事件输出前缀 `OHJSON:` 用于 stdout 上区分协议事件和普通输出（流复用）
- `BackendHost` 内部使用 `Engine::run_streaming()` 并订阅其事件流
- 协议是纯文本 JSON Lines，任何语言的前端都可以实现

## 初学者指南

- 这个模块实现了"前后端分离"——引擎不需要知道前端是 CLI、TUI 还是 Web
- 如果你想写一个自定义前端，只需要实现这个 JSON Lines 协议
- 核心路径：`FrontendRequest::SubmitLine` → `RuntimeBundle::run_prompt()` → `Engine::run_streaming()` → `BackendEvent` → stdout
- 事件流中 `ToolStarted` 和 `ToolCompleted` 让前端可以显示工具执行进度
