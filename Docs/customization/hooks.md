# Hooks

Hooks 允许 CodeHarness 在关键生命周期点执行回调或本地命令。它适合做审计、轻量策略检查、通知，或把工具调用事件转发给本地自动化脚本。

## 配置

Hooks 写在 `settings.json` 的 `hooks` 数组中。

```json
{
  "hooks": [
    {
      "event": "PreToolUse",
      "type": "command",
      "priority": 0,
      "matcher": "bash",
      "block_on_failure": true,
      "timeout_seconds": 5,
      "config": {
        "command": "node D:/codeharness-hooks/check-bash.mjs"
      }
    }
  ]
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `event` | string | 事件名 |
| `type` | string | `callback` 或 `command` |
| `priority` | integer | 同一事件内的排序权重 |
| `matcher` | string | 可选匹配器，例如工具名 |
| `block_on_failure` | boolean | hook 失败时是否阻断 |
| `timeout_seconds` | integer | 命令超时，默认 30 秒 |
| `config` | object | hook 类型相关配置 |

## 事件

当前源码中的事件集合：

| 事件 | 用途 |
| --- | --- |
| `SessionStart` | 会话启动 |
| `SessionEnd` | 会话结束 |
| `PreCompact` | 上下文压缩前 |
| `PostCompact` | 上下文压缩后 |
| `PreToolUse` | 工具执行前 |
| `PostToolUse` | 工具执行后 |
| `UserPromptSubmit` | 用户提交 prompt |
| `Notification` | 通知事件 |
| `Stop` | 模型准备停止 |
| `SubagentStop` | 子 Agent 完成 |

## 返回值

Hook 执行结果包含：

| 字段 | 说明 |
| --- | --- |
| `success` | hook 是否成功 |
| `blocked` | 是否阻断后续流程 |
| `output` | hook 输出 |
| `reason` | 阻断或失败原因 |

对于 `PreToolUse` 这类控制流事件，阻断会阻止工具执行，并把原因传回 engine 的正常错误路径。

## 示例：阻断危险命令

建议在 `PreToolUse` 上匹配 `bash`，由脚本读取工具输入并决定是否阻断。权限系统仍是主要安全边界；hook 适合补充团队策略，不适合替代人工审批。
