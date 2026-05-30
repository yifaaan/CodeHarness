# Tasks C++20 重写方案

Tasks 管理后台运行的 shell 或 agent 任务。它是 subagent、cron、gateway 长任务的基础。

上游关键文件：

- `docs/OpenHarness/src/openharness/tasks/types.py`
- `docs/OpenHarness/src/openharness/tasks/manager.py`
- `docs/OpenHarness/src/openharness/tasks/local_agent_task.py`
- `docs/OpenHarness/src/openharness/tasks/local_shell_task.py`
- `docs/OpenHarness/src/openharness/tasks/stop_task.py`

## TaskRecord

```cpp
struct TaskRecord {
    std::string id;
    std::string type;      // local_bash | local_agent | remote_agent
    std::string status;    // pending | running | completed | failed | killed
    std::string description;
    std::filesystem::path cwd;
    std::filesystem::path outputFile;
    std::optional<std::string> command;
    std::optional<std::string> prompt;
    std::string createdAt;
    std::optional<std::string> startedAt;
    std::optional<std::string> endedAt;
    std::optional<int> returnCode;
    nlohmann::json metadata = nlohmann::json::object();
    std::vector<std::string> argv;
    std::map<std::string, std::string> env;
};
```

## TaskManager

```cpp
class TaskManager {
public:
    TaskRecord createShellTask(ShellTaskSpec spec);
    TaskRecord createAgentTask(AgentTaskSpec spec);
    std::vector<TaskRecord> listTasks() const;
    std::optional<TaskRecord> getTask(std::string_view id) const;
    void writeToTask(std::string_view id, std::string data);
    void stopTask(std::string_view id);
    std::string readOutputTail(std::string_view id, size_t maxBytes = 12000) const;
};
```

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

第一版建议使用 xmake 导入的 `reproc`，再包一层 `ProcessRunner`。`reproc` 已被 awesome-cpp 收录，可以复用现有跨平台子进程实现，同时避免业务模块直接依赖第三方 API。

## 日志和状态持久化

建议目录：

```text
~/.codeharness/data/tasks/
  task-abc.json
  task-abc.log
```

状态变更要写入 JSON，避免程序崩溃后完全丢失。

## Agent task

Agent task 是启动另一个 CodeHarness 进程，例如：

```text
codeharness --task-worker --cwd <path> --prompt <prompt>
```

它可以通过 stdin 接收后续消息，通过 stdout/log 输出结果。

第一版可以先不做可交互 agent task，只做“一次 prompt，一个结果”。

## 工具集成

后续可以提供工具：

- `task_create`
- `task_get`
- `task_list`
- `task_output`
- `task_stop`

这些工具让模型管理后台任务。

## 测试清单

- create shell task 后状态 running。
- 进程结束后状态 completed/failed。
- stdout/stderr 写入 log。
- stopTask 能终止长任务。
- readOutputTail 不读超大文件全部内容。
- Windows 路径和 argv quoting 正确。
