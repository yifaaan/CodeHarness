# Hooks C++20 实现参考

Hooks 模块的 C++20 实现已完成，代码见 `src/codeharness/hooks/`（8 个文件）。

## 已实现的能力

| 能力 | 说明 |
| --- | --- |
| HookEvent 枚举 | `SessionStart`、`SessionEnd`、`PreCompact`、`PostCompact`、`PreToolUse`、`PostToolUse`、`UserPromptSubmit`、`Notification`、`Stop`、`SubagentStop` |
| HookDefinition | event + type (`Command` / `Http`) + priority + matcher + blockOnFailure + timeoutSeconds + config |
| HookRegistry | 按 event 注册 → priority 降序取出，同 priority 保持注册顺序 |
| HookExecutor | `execute(HookEvent, payload)` 遍历匹配 hook，支持 blockOnFailure 阻止工具，返回 `HookExecutionResult{blocked, reason, results}` |
| Command hook | 通过 stdin 接收 payload JSON，timeout，stdout/stderr 捕获 |
| Http hook | POST JSON 到 URL，timeout，2xx=成功，blockOnFailure 可阻止 |
| Tool 名称过滤 | matcher 字段限制挂钩只匹配特定工具（如只对 `bash` 触发 PreToolUse） |
| 与 Engine 集成 | `Engine::execute_tool_use()` 在工具执行前后分别调用 Pre/PostToolUse hooks |

## Hook 类型支持

| 类型 | 状态 | 说明 |
| --- | --- | --- |
| `command` | ✅ | shell 命令，payload 通过 stdin 传入 |
| `http` | ✅ | POST JSON 到 URL |
| `prompt` | 📋 | 调用模型判断 — 暂未实现 |
| `agent` | 📋 | 更彻底的 agent 检查 — 暂未实现 |

## 加载来源

- settings 中显式配置的 hooks
- enabled plugins 中的 hooks（插件 hooks 接入暂未实现）
- Project plugin hooks 默认关闭

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- **热重载**：文件 mtime 轮询或平台文件监控 API
- **Prompt/Agent hook**：避免 hook 与 provider 互相依赖
- `/reload-hooks` 命令
