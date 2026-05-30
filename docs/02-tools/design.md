# Tools 模块设计分析

Tools 模块把模型的结构化 tool call 变成本地动作。

上游关键文件：

- `docs/OpenHarness/src/openharness/tools/base.py`
- `docs/OpenHarness/src/openharness/tools/__init__.py`
- `docs/OpenHarness/src/openharness/tools/*_tool.py`
- `docs/OpenHarness/src/openharness/services/tool_outputs.py`
- `docs/OpenHarness/src/openharness/engine/query.py`

## 工具抽象

上游 `BaseTool` 有四个核心要素：

- `name`：模型调用时使用的工具名。
- `description`：告诉模型工具用途。
- `input_model`：Pydantic 参数模型，用于校验和生成 JSON Schema。
- `execute(arguments, context)`：实际执行。

工具返回统一的 `ToolResult`：

- `output`：给模型看的文本。
- `is_error`：是否失败。
- `metadata`：额外结构化信息。

## ToolRegistry

`ToolRegistry` 是工具表：

```text
tool name -> tool implementation
```

它负责：

- 注册工具。
- 按名称查找工具。
- 列出所有工具。
- 输出 API schema。

模型调用工具时，engine 根据 `ToolUseBlock.name` 到 registry 找工具。

## 默认工具分类

OpenHarness 内置工具很多，可以按功能分组。

| 分类 | 代表工具 | 说明 |
| --- | --- | --- |
| 文件 | `read_file`、`write_file`、`edit_file`、`notebook_edit` | 读写和修改文件 |
| 搜索 | `glob`、`grep`、`web_search`、`web_fetch`、`tool_search`、`lsp` | 本地和网络搜索 |
| 命令 | `bash` | 执行 shell 命令 |
| MCP | `mcp__server__tool`、`list_mcp_resources`、`read_mcp_resource` | 外部工具服务器 |
| 任务 | `task_create`、`task_get`、`task_list`、`task_stop`、`task_output` | 后台任务 |
| 多 agent | `agent`、`send_message`、`team_create`、`team_delete` | subagent 和 team |
| 模式 | `enter_plan_mode`、`exit_plan_mode`、`enter_worktree` | 工作模式切换 |
| 计划 | `todo_write` | agent 自己维护 TODO |
| 元工具 | `skill`、`config`、`brief`、`sleep`、`ask_user` | 加载技能、修改配置、询问用户 |
| 多媒体 | `image_to_text`、`image_generation` | 图片理解和生成 |
| 定时 | `cron_create`、`cron_list`、`cron_delete`、`remote_trigger` | 定时和远程触发 |

## 工具执行生命周期

一次工具调用的大致流程：

```text
ToolUseBlock
  -> registry.get(name)
  -> input validation
  -> PRE_TOOL_USE hooks
  -> permission check
  -> optional user confirmation
  -> tool.execute
  -> output truncation/offload
  -> metadata update
  -> POST_TOOL_USE hooks
  -> ToolResultBlock
```

## 参数校验

Python 版用 Pydantic：

- 校验字段类型。
- 提供默认值。
- 生成 JSON Schema 给模型。

C++20 版需要显式设计。推荐每个工具都提供：

- `input_schema()`：给模型和 validator。
- `parseInput(json)`：把 JSON 转成强类型 struct。

## 只读判断

工具基类有 `is_read_only()`。这是权限系统的重要输入。

例如：

- `read_file` 是只读。
- `grep` 是只读。
- `write_file` 不是只读。
- `bash` 通常不是只读，因为 shell 命令可能修改系统。

默认模式下，只读工具可自动执行，写操作需要确认。

## ToolExecutionContext

上下文包含：

- `cwd`：工具执行的工作目录。
- `metadata`：共享元数据。
- `hook_executor`：必要时工具内部也能触发 hook。

C++20 中建议不要让工具直接访问全局变量，而是通过 context 获取运行环境。

## 工具输出管理

上游 `services/tool_outputs.py` 控制输出大小：

- inline limit：小输出直接回给模型。
- preview limit：大输出只截取开头。
- artifact：完整输出保存到文件。
- microcompact：后续可压缩旧工具结果。

为什么需要？因为模型上下文有限，大输出会撑爆请求。

## Bash 工具安全要点

`bash` 是最危险的工具之一。上游做了这些事：

- 非交互执行。
- stdin 默认关闭。
- timeout。
- 超时后 kill。
- 检测容易卡住的脚手架命令。
- 输出截断。
- 可接入 sandbox。

C++20 重写时，`bash` 工具一定要放到权限系统之后执行。

## 文件工具安全要点

文件工具需要注意：

- 路径相对 cwd 解析。
- 防止 `..` 逃逸。
- 防止 symlink 指向敏感位置。
- Windows 盘符、大小写和 UNC 路径。
- 写文件前可展示 diff。
- edit 失败要返回清晰错误。

## MCP 工具适配

MCP server 提供的工具会被包装成普通工具，命名类似：

```text
mcp__server_name__tool_name
```

这样 engine 不需要区分内置工具和 MCP 工具。

## C++20 初版工具清单

建议第一版只做：

1. `read_file`
2. `write_file`
3. `edit_file`
4. `glob`
5. `grep`
6. `bash`
7. `todo_write`
8. `ask_user`

这些足够跑通基本 coding agent。

## 工具测试重点

- schema 包含正确 required 字段。
- 缺字段或类型错误被拒绝。
- 只读工具 `is_read_only` 返回正确。
- 文件路径不能逃逸 cwd。
- `bash` timeout 能杀进程。
- 工具失败返回 `is_error=true`，不抛到进程顶层。
- 大输出会被截断或保存 artifact。
