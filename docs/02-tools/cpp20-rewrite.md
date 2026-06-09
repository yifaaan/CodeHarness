# Tools C++20 实现参考

Tools 模块的 C++20 实现已完成，共 ~28 个文件在 `src/codeharness/tools/`。

## 已实现的工具

| 工具 | 类 | 位置 | 备注 |
| --- | --- | --- | --- |
| `read_file` | `ReadFileTool` | `tools/read_file_tool.h/.cpp` | offset/limit 行范围，行号输出 |
| `write_file` | `WriteFileTool` | `tools/write_file_tool.h/.cpp` | atomic write (.tmp + rename)，父目录自动创建 |
| `edit_file` | `EditFileTool` | `tools/edit_file_tool.h/.cpp` | old_string→new_string，`replace_all` 支持 |
| `glob` | `GlobTool` | `tools/glob_tool.h/.cpp` | `p-ranav-glob` 匹配 |
| `grep` | `GrepTool` | `tools/grep_tool.h/.cpp` | `re2` 正则，跳过二进制/大文件 |
| `bash` | `BashTool` | `tools/bash_tool.h/.cpp` | `reproc` 子进程，timeout，输出截断 |
| `ask_user` | `AskUserTool` | `tools/ask_user_tool.h/.cpp` | 只读，触发 UI 问答 |
| `todo_write` | `TodoWriteTool` | `tools/todo_write_tool.h/.cpp` | 任务列表管理 |
| `skill` | `SkillTool` | `skills/skill_tool.h/.cpp` | 按需加载 skill 内容 |
| `agent` | `AgentTool` | `tasks/task_tools.h/.cpp` | 创建 subprocess worker |
| `task_create/get/list/output/stop` | 各 TaskTool | `tasks/task_tools.h/.cpp` | 后台任务管理 |
| `send_message` | `SendMessageTool` | `mailbox/mailbox_tools.h/.cpp` | 向 task inbox 投递消息 |

## 核心抽象

```
Tool (抽象基类)
├── name() / description() / input_schema()
├── is_read_only(json args) → bool
├── permission_target(json args) → PermissionTarget
├── execute(json args, ToolContext) → ToolResponse

ToolRegistry
├── add(shared_ptr<Tool>) → 按 name 注册
├── find(name) → shared_ptr<Tool>
├── execute(name, args, ctx) → ToolResponse
├── names() → 所有已注册工具名
```

`ToolContext` 包含 `cwd`、`metadata`、`HookExecutor*`。
`ToolResponse` 包含 `output` 文本、`is_error`、`metadata`。

## Workspace 安全

路径安全通过 `tools/workspace_path.h/.cpp` 统一处理：

- `resolve_workspace_path(cwd, user_path)` — 防止 `..` 逃逸
- `is_under_directory(path, parent)` — 严格前缀检查
- 所有文件工具在 `execute()` 入口处调用此函数

## Permission 目标提取

`permission_target.h/.cpp` 为每个工具提取 `PermissionTarget{path, command}`，供 `PermissionChecker` 决策。

## 工具注册

工具注册在 `runtime/runtime.cpp` 的 `create_tool_registry()` 中完成。内置工具 + MCP 工具 + Task/Mailbox 工具统一注册。

## 工具输出管理

当前策略：输出小于 ~16000 字符 inline 返回；超过则保存 artifact 并返回 preview + 路径。策略在 `ToolResponse` 的 metadata 字段中传递。

## 已知问题

- `WriteFileTool` 已实现但**未注册**到 `create_tool_registry()` — 仅有 `ReadFileTool` 和 `EditFileTool` 可用于文件写入

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- 动态加载 C++ 插件工具 `.dll/.so`
- 完整 Jupyter notebook 编辑（根据用户反馈不需要）
- 图片生成 / 图像处理工具
- LSP 工具
- cron / remote trigger
- WebFetch / WebSearch 工具
- EnterWorktree / ExitWorktree 工具（根据用户反馈暂不需要）
