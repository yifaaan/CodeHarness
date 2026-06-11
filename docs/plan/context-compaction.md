# 上下文压缩计划

## 目标

为长会话提供手动和自动上下文压缩，降低 token 占用并保留关键工作状态。

## 当前缺口

- Hook 事件中已有 `PreCompact` 和 `PostCompact`。
- 当前没有用户可见的 `/compact` 命令。
- 没有压缩摘要写回会话的完整流程。

## 建议落点

- `src/codeharness/engine/`：定义压缩输入、摘要消息和继续运行策略。
- `src/codeharness/commands/command_registry.*`：注册 `/compact [instruction]`。
- `src/codeharness/sessions/`：保存压缩后的会话状态。
- `src/codeharness/hooks/`：确保压缩前后事件 payload 稳定。

## 验收标准

- `/compact` 可在空闲状态触发。
- 可选 instruction 会传给压缩 prompt。
- 压缩前后触发 hooks。
- 压缩失败不会破坏原会话。
