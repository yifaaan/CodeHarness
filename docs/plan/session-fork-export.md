# 会话 fork 与导出计划

## 目标

支持从历史会话恢复、派生新会话、设置标题和导出会话，完善长线 coding-agent 工作流。

## 当前缺口

- 已有 `/sessions` 和 `/resume <id>`。
- 尚无 `/fork`、`/title`、`/rename`、`export` 等用户入口。
- 会话导出格式和脱敏策略尚未定义。

## 建议落点

- `src/codeharness/sessions/`：扩展 snapshot 元数据、标题和 fork 来源。
- `src/codeharness/commands/command_registry.*`：注册 fork/title/export 命令。
- `src/codeharness/tui/`：会话列表、恢复和导出确认 UI。

## 验收标准

- 当前会话可 fork 为新 session id。
- 用户可设置和查看标题。
- 导出文件包含 transcript、工具调用摘要和必要元数据。
- 导出默认不包含 API key 或敏感配置。
