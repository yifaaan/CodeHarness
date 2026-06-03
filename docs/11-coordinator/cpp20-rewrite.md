# Coordinator 和 Swarm C++20 重写方案

Coordinator/Swarm 负责多 agent 协作。它是高级功能，建议在核心 engine、tools、tasks 稳定后再实现。

上游关键文件：

- `docs/OpenHarness/src/openharness/coordinator/coordinator_mode.py`
- `docs/OpenHarness/src/openharness/coordinator/agent_definitions.py`
- `docs/OpenHarness/src/openharness/swarm/types.py`
- `docs/OpenHarness/src/openharness/swarm/registry.py`
- `docs/OpenHarness/src/openharness/swarm/subprocess_backend.py`
- `docs/OpenHarness/src/openharness/swarm/in_process.py`
- `docs/OpenHarness/src/openharness/swarm/mailbox.py`
- `docs/OpenHarness/src/openharness/swarm/team_lifecycle.py`
- `docs/OpenHarness/src/openharness/swarm/worktree.py`

## Coordinator 是什么

Coordinator 是“总指挥 agent”。它负责：

- 拆分任务。
- 创建 worker agent。
- 给 worker 明确 prompt。
- 收集结果。
- 综合判断下一步。

Worker 不共享 coordinator 的完整上下文，所以 coordinator 给 worker 的 prompt 必须自包含。

## AgentDefinition

```cpp
struct AgentDefinition {
    std::string name;
    std::string description;
    std::string systemPrompt;
    std::vector<std::string> tools;
    std::vector<std::string> disallowedTools;
    std::optional<std::string> model;
    std::optional<std::string> effort;
    std::optional<PermissionMode> permissionMode;
    std::optional<int> maxTurns;
    std::vector<std::string> skills;
    std::vector<std::string> mcpServers;
    std::string source;
};
```

来源：

- builtin agents。
- user agents：`~/.openharness/agents/*.md`。
- plugin agents。

## Task notification

上游 worker 完成时用 XML 包装结果：

```xml
<task-notification>
<task-id>task-123</task-id>
<status>completed</status>
<summary>...</summary>
<result>...</result>
</task-notification>
```

C++ 可以用更结构化 JSON，但如果要兼容上游 prompt，可以保留 XML 格式。

## Swarm backend

建议先实现 subprocess backend：

```text
Coordinator
  -> agent tool
  -> SubprocessBackend.spawn
  -> TaskManager.createAgentTask
  -> worker CodeHarness process
```

后续再做 in-process backend。

## Mailbox

上游 mailbox 是文件系统队列：

```text
~/.openharness/teams/<team>/agents/<agent_id>/inbox/<timestamp>_<message_id>.json
```

每条消息一个 JSON 文件。

C++ 保留这个设计有好处：

- 跨进程简单。
- 崩溃后消息仍在。
- 可手工调试。
- 不需要本地服务器。

写入规则：

1. 加锁。
2. 写 `.tmp`。
3. flush。
4. rename 成 `.json`。

## Team lifecycle

团队元数据：

```text
~/.openharness/teams/<team>/team.json
```

包含：

- team name。
- lead agent id。
- members。
- allowed paths。
- hidden panes。
- metadata。

C++ 应使用 team-level lock 防止并发 read-modify-write 丢更新。

## Worktree isolation

为避免多个 agent 改同一工作区，可以给 worker 创建 git worktree：

```text
~/.openharness/worktrees/<slug>/
```

slug 校验必须严格：

- 非空。
- 不允许绝对路径。
- 不允许 `.`、`..`。
- 只能包含 `[a-zA-Z0-9._-]`。
- 长度限制。

## 权限同步

Worker 请求高风险工具时不能直接问用户。流程：

```text
worker sends permission_request to leader mailbox
leader evaluates and maybe asks user
leader sends permission_response
worker continues
```

第一版可以先不做 worker 权限同步，强制 worker 只读。

## 实现路线

1. `AgentDefinitionLoader`。
2. `TaskManager.createAgentTask`，基于当前已实现的 `TaskManager.create_shell_task` 包装一次性 worker。
3. `task_create`、`task_get`、`task_list`、`task_output`、`task_stop` 工具接入 ToolRegistry。
4. `agent` 工具创建 subprocess worker。
5. `Mailbox`。
6. `send_message` 工具。
7. `TeamLifecycleManager`。
8. `WorktreeManager`。
9. 权限同步。

## 初学者建议

不要过早实现多 agent 写代码。先让 worker 只做研究任务，例如“分析某模块结构并返回总结”。等权限、worktree、merge 策略稳定后，再允许多个 agent 改文件。
