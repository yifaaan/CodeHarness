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

CodeHarness 当前实现位于 `src/codeharness/coordinator/agent_definition.*`，第一版只做“把 Markdown agent 定义加载成结构化数据”，不负责启动 worker、不管理 mailbox、不做权限同步。这让后续 SubprocessBackend 可以先复用一个稳定的数据入口。

```cpp
struct AgentDefinition {
    std::string name;
    std::string description;
    std::string system_prompt;
    std::vector<std::string> tools;
    std::vector<std::string> disallowed_tools;
    std::optional<std::string> model;
    std::optional<std::string> effort;
    std::optional<std::string> permission_mode;
    std::optional<int> max_turns;
    std::vector<std::string> skills;
    std::vector<std::string> mcp_servers;
    std::string source;
    std::filesystem::path path;
    std::filesystem::path base_dir;
};
```

当前支持的定义文件格式是 Markdown + YAML frontmatter：

```markdown
---
name: reviewer
description: Review changed C++ files
tools: [read_file, grep, glob]
disallowed_tools: [bash]
model: claude-sonnet-4-6
max_turns: 8
skills: [review]
---
You are a careful C++ reviewer...
```

加载能力：

- `parse_agent_definition_markdown`：解析单个字符串，frontmatter 后正文作为 `system_prompt`。
- `load_agent_definition_file`：读取单个 `.md` 文件，并记录 `path`/`base_dir`。
- `load_agent_definitions_from_dirs`：从多个目录扫描 `*.md`，按路径排序并去重。
- `discover_project_agent_dirs`：从 `cwd` 向上查找 project agent 目录，最多到 git 根；返回顺序是父目录到子目录，方便近目录覆盖远目录。
- `load_agent_definitions`：组合 user → extra → project 来源。

当前支持来源：

- user agents：`~/.codeharness/agents/*.md`、`~/.openharness/agents/*.md`、`~/.claude/agents/*.md`、`~/.agents/agents/*.md`。
- project agents：`.codeharness/agents/*.md`、`.openharness/agents/*.md`、`.agents/agents/*.md`、`.claude/agents/*.md`。
- extra agents：调用方显式传入目录。

builtin/plugin agents 暂未接入 registry；等 SubprocessBackend/agent registry 需要统一覆盖策略时再补，避免当前阶段过早抽象。

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

当前 C++ 实现位于 `src/codeharness/mailbox/team_lifecycle.*`。第一版采用独立 teams 根目录：

```text
~/.codeharness/data/teams/<team>/team.json
```

当前持久化字段：

- team name。
- description。
- created_at。
- lead_agent_id。
- members：`agent_id -> TeamMember`，成员包含 `agent_id`、`name`、`backend_type`、`joined_at`。

已支持能力：

- 创建/删除团队。
- 读取单个团队。
- 列出所有团队，按名称排序。
- 添加/移除成员。
- 设置 leader agent。
- 原子写入 `team.json`，使用 `.tmp -> rename`，复用 `atomic_write_text_file`。

尚未实现：

- allowed paths。
- hidden panes。
- metadata 扩展字段。
- team-level 文件锁；当前 read-modify-write 仍假设低并发 coordinator 操作。

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

1. `AgentDefinitionLoader`。已完成第一版：Markdown + YAML frontmatter 解析、user/extra/project 目录加载和 focused tests。
2. `TaskManager.createAgentTask`，基于当前已实现的 `TaskManager.create_shell_task` 包装一次性 worker。已完成。
3. `task_create`、`task_get`、`task_list`、`task_output`、`task_stop` 工具接入 ToolRegistry。已完成，当前 `task_create` 支持 `local_bash` 和一次性 `local_agent`。
4. `agent` 工具创建 subprocess worker。已完成最小版，当前仅支持 `local_agent` mode。
5. `Mailbox`。已完成第一版文件系统 inbox、poll、mark_read、clear。
6. `send_message` 工具。已完成，支持向 task inbox 投递消息并可选校验 recipient task。
7. `TeamLifecycleManager`。已完成第一版 team.json CRUD 和成员管理。
8. `WorktreeManager`。暂不实现；当前项目阶段先不移植 worktree tool。
9. 权限同步。未实现；第一版 worker 仍应以只读/受限权限为主。

当前下一步应转向 subprocess swarm backend/agent registry 的最小版：把 `AgentDefinitionLoader`、`TaskManager.create_agent_task`、`Mailbox` 和 `TeamLifecycleManager` 串起来，形成“按 agent 定义创建一次性 worker 并记录 team membership”的基础路径。

## 初学者建议

不要过早实现多 agent 写代码。先让 worker 只做研究任务，例如“分析某模块结构并返回总结”。等权限、worktree、merge 策略稳定后，再允许多个 agent 改文件。
