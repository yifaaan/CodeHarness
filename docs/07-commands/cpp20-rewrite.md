# Commands C++20 实现参考

Slash Commands 模块的 C++20 实现已完成，代码见 `src/codeharness/commands/command_registry.h/.cpp`。

## 已实现的能力

| 能力 | 说明 |
| --- | --- |
| `CommandRegistry` | `register_command()` / `lookup()` / `list()`，支持 name + aliases 匹配 |
| `SlashCommand` 结构 | name、description、handler、aliases、remoteInvocable |
| `CommandResult` | message、shouldExit、clearScreen、refreshRuntime、submitPrompt、submitModel |
| `CommandContext` | runtime + cwd 传入 handler |
| 内置命令 | `/help`、`/clear`、`/exit`、`/model`、`/skills`、`/permissions`、`/memory`、`/tasks`、`/mcp` |
| 用户输入分流 | 在 `cli/cli.cpp` 中：以 `/` 开头 → lookup → 执行 handler → 处理 CommandResult；否则 → `Engine::submit_message()` |
| Skill slash command | `userInvocable=true` 的 skill 自动注册为 `/skillname`，支持 `$ARGUMENTS` 替换 |

## 内置命令列表

| 命令 | 功能 |
| --- | --- |
| `/help` | 列出所有可用命令 |
| `/clear` | 清空当前会话历史 |
| `/exit` | 退出程序 |
| `/model` | 显示或切换当前模型 |
| `/skills` | 列出已加载的 skills |
| `/memory` | 记忆管理（添加/列表/移除） |
| `/permissions` | 显示或切换权限模式 |
| `/tasks` | 后台任务管理 |
| `/mcp` | MCP server 状态 |

## 用户输入分流逻辑

```
输入以 "/" 开头
  → CommandRegistry::lookup() 匹配
  → 匹配 → 执行 handler，处理 CommandResult
  → 不匹配但存在同名 skill → 构建 skill prompt
  → 都不匹配 → 显示未知命令
否则 → Engine::submit_message()
```

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- Plugin command namespace（如 `/git:commit`）
- 高风险命令的远程会话限制
