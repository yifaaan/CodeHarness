# 后台任务体验计划

## 目标

完善后台任务的 TUI 展示、通知和生命周期控制，让长时间命令与子 Agent 工作更容易观察和管理。

## 当前已有

- `task_create`
- `task_list`
- `task_get`
- `task_output`
- `task_stop`
- `agent`
- 任务记录和输出文件落盘

## 当前缺口

- TUI 中缺少完整任务面板。
- 后台任务完成通知还需要更清晰的 UI 集成。
- 任务超时、保活、并发限制等配置尚未形成稳定用户配置文档。

## 建议落点

- `src/codeharness/tasks/`：完善任务配置和状态字段。
- `src/codeharness/tui/`：任务列表和输出查看面板。
- `src/codeharness/hooks/`：任务完成通知 payload 稳定化。
- `src/codeharness/config/`：后台任务策略配置。

## 验收标准

- TUI 可查看运行中和已完成任务。
- 用户可停止任务并看到停止原因。
- 任务输出支持分页查看。
- 后台 Agent 完成后自动回传主会话。
