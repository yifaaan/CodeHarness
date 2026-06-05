# tasks/ — 任务管理器模块

## 设计目标

管理智能体创建的各类子任务，支持后台 shell 命令、本地子智能体、远程智能体等任务类型。为多智能体和异步操作提供基础。

## 架构

```
TaskManager
  ├─ create_shell_task(command)     ← 后台 shell 任务
  ├─ create_agent_task(config)      ← 子智能体任务
  ├─ list_tasks()                   ← 列出所有任务
  ├─ stop_task(id)                  ← 停止任务
  ├─ read_output_tail(id)          ← 读取任务输出尾部
  └─ wait_for_task(id)             ← 等待任务完成

任务类型：
  ├─ LocalBash      ← 本地 shell 命令
  ├─ LocalAgent     ← 本地子智能体
  └─ RemoteAgent    ← 远程智能体（预留）

TaskTools（LLM 可调用的工具集）
  ├─ TaskCreateTool  ← 创建任务
  ├─ TaskListTool    ← 列出任务
  ├─ TaskGetTool     ← 查询任务状态
  ├─ TaskOutputTool  ← 读取任务输出
  ├─ TaskStopTool    ← 停止任务
  └─ AgentTool       ← 通过 coordinator 创建子智能体
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `TaskManager` | 任务生命周期管理，持久化任务状态到磁盘 |
| `AgentSpawnRequest` / `AgentSpawnResponse` | 智能体孵化请求和响应 |
| `TaskTool` 系列 | 将任务操作暴露为 Tool 接口供 LLM 调用 |

## 设计要点

- 任务状态持久化到磁盘，支持进程重启后恢复
- 任务输出支持尾部读取（`read_output_tail`），避免大输出内存溢出
- `AgentTool` 通过 `coordinator/` 的 `SubprocessBackend` 孵化了进程智能体

## 初学者指南

- 任务模块是智能体"动手做事"的通道——shell 命令和子智能体都通过任务系统管理
- `TaskCreateTool` 和 `BashTool` 的区别：BashTool 是同步的（等命令执行完），`TaskCreateTool` 是异步的（后台运行）
- 任务可组合：一个智能体可以创建子任务，子任务又可以创建更多子任务
