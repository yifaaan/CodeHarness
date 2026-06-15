# Hooks

Hooks 允许 CodeHarness 在关键生命周期点执行本地命令（子进程）。它适合做审计、轻量策略检查、通知，或把工具调用事件转发给本地自动化脚本。

## 配置

Hooks 写在 `config.toml` 的 `[[hooks]]` 数组中（和 `[providers]` / `[models]` 一样）。配置路径解析见 [配置文件](../configuration/config-files.md)。

```toml
[[hooks]]
event = "PreToolUse"
command = "node D:/codeharness-hooks/check-bash.mjs"
matcher = "Bash"
timeout = 5

[[hooks]]
event = "SessionEnd"
command = "sh ~/.codeharness/hooks/on-close.sh"
```

| 字段 | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `event` | string | （必填） | 事件名，见下表 |
| `command` | string | （必填） | 要执行的命令；按空格拆分为 argv（无 shell），需要管道/重定向请显式包裹 `sh -c "..."` / `cmd /c "..."` |
| `matcher` | string | 空（匹配全部） | 正则表达式，匹配事件的 target（工具事件为工具名、会话事件为 sessionId） |
| `timeout` | integer | 30 | 超时秒数，范围 1..600 |

## 执行模型

每个匹配的 hook 会被 spawn 为子进程：

1. 一个 JSON 对象写到 hook 的 stdin，形如 `{"event":"PreToolUse","target":"Bash",...}`，其中 target 是 matcher 的匹配对象，其余字段按事件类型携带（`toolCall`、`result`、`session`、`error` 等）。
2. hook 的 stdout/stderr 被捕获，并施加 per-hook 超时。
3. 退出码 0 视为成功。

## 失败即放行（Fail-Open）

**这是硬性不变量。** hook 命令失败（非零退出、超时、崩溃）时，一律视为 **Allow**（放行）。一个写坏的脚本永远不会卡住或阻断 agent。唯一的阻断方式是 hook 在 stdout 打印一行 JSON：

```json
{"action":"block","reason":"这条命令被策略禁止"}
```

并且只有 2 个**阻断型**事件会消费这个决定：`PreToolUse` 和 `UserPromptSubmit`。其余 9 个事件是纯通知，忽略 `action`。

> 安全提示：需要 fail-closed 的安全边界请使用 [权限系统](permission-and-hooks.md)（Manual 模式下无回调即拒绝），而不是 hook。hook 适合补充团队策略与审计，不适合替代人工审批。

## 事件

当前实现 13 个事件中的 11 个；`SubagentStart` / `SubagentStop` 待子 agent 模块上线后接入。

| 事件 | 触发点 | 可阻断 | target |
| --- | --- | --- | --- |
| `PreToolUse` | 工具执行前 | ✅ | 工具名 |
| `PostToolUse` | 工具成功返回后 | — | 工具名 |
| `PostToolUseFailure` | 工具出错后 | — | 工具名 |
| `UserPromptSubmit` | prompt 送入 agent 前 | ✅ | （空） |
| `Stop` | 一轮正常结束 | — | （空） |
| `StopFailure` | 一轮以错误结束 | — | （空） |
| `SessionStart` | 会话创建/恢复后 | — | sessionId |
| `SessionEnd` | 会话关闭前 | — | sessionId |
| `PreCompact` | 上下文压缩前 | — | （空） |
| `PostCompact` | 上下文压缩后 | — | （空） |
| `Notification` | 通用通知（由调用方触发） | — | 自定义 |

## 示例：审计所有 Bash 调用

```toml
[[hooks]]
event = "PostToolUse"
command = "sh ~/.codeharness/hooks/log-bash.sh"
matcher = "Bash"
```

`log-bash.sh` 从 stdin 读 JSON，把命令和结果追加到日志：

```sh
#!/bin/sh
input=$(cat)
echo "$input" >> ~/.codeharness/hooks/bash-audit.jsonl
```

## 示例：阻断特定命令（阻断型）

```toml
[[hooks]]
event = "PreToolUse"
command = "node ~/.codeharness/hooks/guard-bash.mjs"
matcher = "Bash"
timeout = 5
```

`guard-bash.mjs` 检查 stdin 的 JSON，若包含危险命令就打印阻断决定：

```js
let input = "";
process.stdin.on("data", (c) => (input += c));
process.stdin.on("end", () => {
  const ctx = JSON.parse(input);
  const cmd = ctx.toolCall?.args?.command ?? "";
  if (/rm\s+-rf/.test(cmd)) {
    console.log(JSON.stringify({ action: "block", reason: "禁止 rm -rf" }));
    process.exit(0); // 退出码 0；阻断由 stdout 的 JSON 决定
  }
  process.exit(0); // 不打印 JSON = Allow
});
```
