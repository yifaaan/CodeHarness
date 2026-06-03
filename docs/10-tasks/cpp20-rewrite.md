# Tasks C++20 重写方案

Tasks 管理后台运行的 shell 或 agent 任务。它是 subagent、cron、gateway 长任务的基础。

上游关键文件：

- `docs/OpenHarness/src/openharness/tasks/types.py`
- `docs/OpenHarness/src/openharness/tasks/manager.py`
- `docs/OpenHarness/src/openharness/tasks/local_agent_task.py`
- `docs/OpenHarness/src/openharness/tasks/local_shell_task.py`
- `docs/OpenHarness/src/openharness/tasks/stop_task.py`

## TaskRecord

CodeHarness 当前实现位于 `src/codeharness/tasks/task_manager.h`，类型已 enum 化，但 JSON 持久化仍使用上游兼容的字符串值。

```cpp
struct TaskRecord {
    std::string id;
    TaskType type;         // local_bash | local_agent | remote_agent
    TaskStatus status;     // pending | running | completed | failed | killed
    std::string description;
    std::filesystem::path cwd;
    std::filesystem::path output_file;
    std::optional<std::string> command;
    std::optional<std::string> prompt;
    std::string created_at;
    std::optional<std::string> started_at;
    std::optional<std::string> ended_at;
    std::optional<int> return_code;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
};
```

## TaskManager

当前 C++ v1 已实现 shell task 的后台执行、一次性 local agent task、状态落盘、输出 tail 和停止；stdin 写入、completion listener 暂未实现。

```cpp
class TaskManager {
public:
    Result<TaskRecord> create_shell_task(const ShellTaskSpec& spec);
    Result<TaskRecord> create_agent_task(const AgentTaskSpec& spec);
    Result<std::vector<TaskRecord>> list_tasks(std::optional<TaskStatus> status = std::nullopt) const;
    Result<std::optional<TaskRecord>> get_task(std::string_view id) const;
    Result<TaskRecord> stop_task(std::string_view id);
    Result<std::string> read_output_tail(std::string_view id, size_t max_bytes = 12000) const;
    Result<TaskRecord> wait_for_task(std::string_view id);
};
```

## 和 OpenHarness 对比进度

| 能力 | OpenHarness | CodeHarness 当前状态 |
| --- | --- | --- |
| `TaskRecord` | dataclass，字段包含 type/status/cwd/output/env/argv | 已实现 C++ struct + enum，JSON 落盘字段保持 snake_case |
| shell task | asyncio 子进程后台运行，stdout/stderr 合并到 log | 已实现 `reproc` 后台线程，stdout/stderr 合并写 `.log` |
| task 持久化 | 进程内记录为主，输出文件在 data dir | 已实现 `~/.codeharness/data/tasks` 默认路径和 `<id>.json`/`<id>.log` |
| `argv` 直启 | 支持，避免 Windows shell quoting 问题 | 已支持 `argv` 和 `command` 二选一 |
| 环境变量 | task env 合并到父进程环境 | 已支持 `env.extra`，继承父环境 |
| stop task | terminate 后必要时 kill | 已实现 kill + join，状态落为 `killed` |
| agent task | 可启动本地 agent，并向 stdin 写 prompt | 已实现一次性 `local_agent` task；默认启动 `codeharness -p <prompt> --cwd <cwd>`，也支持 `command`/`argv` 覆盖 |
| `write_to_task` | 支持向 agent stdin 写入，并可重启 agent | 未实现 |
| completion listener | 支持任务完成回调 | 未实现 |
| task tools | `task_create/get/list/output/stop` | 已接入 ToolRegistry；`task_create` 支持 `local_bash` 和一次性 `local_agent` |
| `agent` tool | 创建 subprocess worker，并返回 task id | 已接入 ToolRegistry；当前仅支持 `local_agent` mode |

当前进度结论：Tasks 模块进入 Phase 5 的基础可用状态，可以支撑后续 subprocess swarm backend、Mailbox 和 `send_message`，但还不是 OpenHarness 的完整后台任务系统。

## 进程模型

每个后台 task 通常对应一个子进程：

```text
TaskManager
  -> ProcessHandle
  -> stdout/stderr reader
  -> output log file
  -> status update
```

stdout 和 stderr 建议合并写入同一个 log，便于 UI 显示。

## 跨平台难点

Windows：

- `CreateProcessW`。
- stdin/stdout/stderr pipe。
- 进程组和终止。

Linux/macOS：

- `fork/exec` 或 `posix_spawn`。
- pipe。
- process group。

第一版已使用 vcpkg manifest 导入的 `reproc`。Windows 下避免 `nonblocking` pipe 组合，采用 `poll()` + bounded read 的方式读取 stdout，和 MCP stdio transport 的跨平台策略保持一致。

## 日志和状态持久化

当前默认目录：

```text
~/.codeharness/data/tasks/
  b1234abcd.json
  b1234abcd.log
```

状态变更要写入 JSON，避免程序崩溃后完全丢失。

## Agent task

Agent task 是启动另一个 CodeHarness 进程，例如：

```text
codeharness -p <prompt> --cwd <path>
```

当前 C++ 第一版是“一次 prompt，一个结果”的一次性 agent task。它通过 stdout/log 输出结果；stdin 写入、自动重启和持续对话留给后续 `write_to_task`。

`model` 当前只记录到 metadata；CLI/provider profile 接入后再让子进程真实继承模型配置。

## 工具集成

当前已提供工具：

- `task_create`
- `task_get`
- `task_list`
- `task_output`
- `task_stop`
- `agent`

这些工具让模型管理后台任务。`task_create` 支持 `local_bash` 与 `local_agent`，两者都支持 `command` 或 `argv` 覆盖、`description`、可选 `env`；`local_agent` 还要求 `prompt`。`task_get`、`task_list`、`task_stop` 返回 TaskRecord JSON，`task_output` 返回日志 tail 文本。`agent` 工具是上游 agent tool 的最小形态，创建一个 `local_agent` task 并返回 `agent_id`、`task_id`、`backend_type`。

## 测试清单

- create shell task 后状态 running。已覆盖。
- 进程结束后状态 completed/failed。已覆盖。
- stdout/stderr 写入 log。已覆盖 stdout，stderr 合并由 redirect 配置负责。
- stopTask 能终止长任务。已覆盖。
- readOutputTail 不读超大文件全部内容。已覆盖 tail 读取。
- task tools 可创建、列出、读取详情、读取输出和停止任务。已覆盖。
- local_agent task 与 agent tool。已覆盖一次性 worker 创建、记录落盘和输出读取。
- Windows 路径和 argv quoting 正确。已覆盖 `argv` 直启路径，后续补 shell command quoting 回归测试。
