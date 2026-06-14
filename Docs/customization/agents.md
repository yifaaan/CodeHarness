# Agent 与子 Agent

CodeHarness 的子 Agent 能力通过内置工具 `agent` 和后台任务系统提供。它让主 Agent 把一段工作委托给隔离的本地 Agent 任务，并通过任务记录和结果回传继续协作。

## 调用方式

模型可通过 `agent` 工具创建子任务。常用输入包括：

| 字段 | 说明 |
| --- | --- |
| `prompt` | 子 Agent 要执行的完整任务 |
| `description` | 用于 UI 或任务列表的简短说明 |
| `subagent_type` | 子 Agent 类型，当前主要作为任务元数据 |
| `model` | 可选模型标识 |
| `run_in_background` | 是否后台运行 |

后台任务可用 `task_list`、`task_get`、`task_output` 和 `task_stop` 查询与控制。

## 上下文隔离

子 Agent 任务有自己的任务记录和输出文件，不会直接复用主会话完整 transcript。主 Agent 通过任务结果继续工作。

## 权限继承

子 Agent 仍通过同一套工具、权限和 hook 机制执行。写入、命令执行、任务停止等高风险操作仍会被权限系统评估。

## 存储位置

任务记录默认存放在 `<data_dir>/tasks`。任务输出也会落盘，`task_output` 会返回最近输出片段和完整输出路径。
