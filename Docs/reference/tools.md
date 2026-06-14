# 内置工具

CodeHarness 的工具实现统一继承 `Tool` 接口，并注册到 `ToolRegistry`。Engine 收到模型的 tool use 后，会先经过 hook 和权限检查，再执行工具，并把结果作为 `ToolResultBlock` 回填给模型。

## 文件类

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `read_file` | 只读 | 读取工作区内文本文件 |
| `write_file` | 写入 | 创建或覆盖文件 |
| `edit_file` | 写入 | 基于精确字符串替换文件内容 |
| `glob` | 只读 | 按 glob 模式查找文件 |
| `grep` | 只读 | 使用 RE2 正则搜索文件内容 |

文件路径会通过 workspace path 解析，绝对路径和逃逸出 cwd 的路径会被拒绝。

## Shell

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `bash` | 执行 | 执行 shell 命令 |

`bash` 接受命令、工作目录和超时等输入。命令执行失败会转为工具错误结果，而不是让 harness 崩溃。

## 网络类

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `web_search` | 只读 | 搜索网页并返回标题与 URL |
| `web_fetch` | 只读 | 抓取单个网页并返回紧凑文本 |

网络工具会校验 URL，阻止明显的内部网络 SSRF 风险。

## 状态与等待

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `todo_write` | 写入 | 维护任务待办列表 |
| `sleep` | 只读 | 等待指定秒数，最大 300 秒 |

## 协作类

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `ask_user` | 只读 | 向用户提出结构化问题 |
| `skill` | 只读 | 调用允许模型调用的 Skill |
| `agent` | 任务 | 创建子 Agent 任务 |
| `send_message` | 协作 | 向任务 mailbox 发送消息 |

## 后台任务

| 工具 | 默认权限倾向 | 说明 |
| --- | --- | --- |
| `task_create` | 执行 | 创建本地 bash 或 agent 任务 |
| `task_list` | 只读 | 列出任务 |
| `task_get` | 只读 | 查看单个任务记录 |
| `task_output` | 只读 | 查看任务输出 |
| `task_stop` | 执行 | 停止任务 |

## 权限行为

每个工具声明是否只读，并可提供路径或命令权限目标。`PermissionChecker` 根据当前模式、工具名、路径规则、命令规则和 session grants 产生 `allow`、`ask` 或 `deny`。

## MCP 工具

MCP 工具会适配为内部工具，命名形如：

```text
mcp__<server>__<tool>
```

权限和内置工具一致。
