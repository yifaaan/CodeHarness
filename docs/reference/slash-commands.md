# 斜杠命令

斜杠命令是 CodeHarness 在交互式或 `--prompt` 路径中提供的控制入口。输入以 `/` 开头的内容时，运行时会先尝试匹配内置命令、Plugin 命令或 Skill 命令。

## 模式与权限

| 命令 | 别名 | 说明 |
| --- | --- | --- |
| `/plan` | — | 进入 Plan 模式，只读分析 |
| `/act` | `/plan off` 在 CLI 特判路径中可用 | 回到默认权限模式 |
| `/fullauto` | `/full_auto` | 进入 full-auto 权限模式 |
| `/default` | — | 回到默认权限模式 |
| `/mode` | `/permissions` 在 CLI 特判路径中可用 | 显示当前权限模式 |

## Skills

| 命令 | 说明 |
| --- | --- |
| `/skills` | 列出已加载 Skills |
| `/<skill-command>` | 调用 user-invocable Skill |

Skill 命令会渲染为 prompt 并提交给 agent loop。Skill 可以指定 `model`，运行时会尝试切换到对应 profile。

## Memory

| 命令 | 说明 |
| --- | --- |
| `/memory` | 默认列出 memories |
| `/memory list` | 列出 memories |
| `/memory add TITLE :: BODY` | 新增 memory |
| `/memory search QUERY` | 搜索 memory |
| `/memory remove NAME_OR_ID` | 删除 memory |

## Sessions

| 命令 | 说明 |
| --- | --- |
| `/sessions` | 列出历史会话 |
| `/sessions <limit>` | 限制返回数量 |
| `/resume <session-id>` | 恢复指定会话 |

## Plugins

| 命令 | 说明 |
| --- | --- |
| `/plugin` | 默认列出 plugins |
| `/plugin list` | 列出 plugins |
| `/<plugin-command>` | 调用 plugin 提供的命令 |

Plugin 命令可以返回一段消息，也可以提交 prompt 给 agent loop。

## 未实现命令

登录、设置面板、主题、外部编辑器、会话 fork/export、上下文压缩等命令尚未在当前源码中作为用户命令落地。计划见 [命令补齐计划](../plan/kimi-style-command-parity.md)。
