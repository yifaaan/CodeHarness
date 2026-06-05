# commands/ — 斜杠命令系统

## 设计目标

为用户提供一组聊天式快捷命令（如 `/help`、`/skills`、`/model`），在交互过程中快速切换配置或查看状态。

## 架构

```
execute_slash_command(name, args)
  └─ CommandRegistry::find(name)
       └─ CommandHandler callback
            └─ 返回 Message 或直接输出
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `Command` | 命令定义：name、description、aliases、handler |
| `CommandRegistry` | 按名称/别名注册和查找命令 |
| `CommandHandler` | `std::function<Result<Message>(ParsedArgs)>` |
| `build_builtin_command_registry()` | 注册所有内置命令 |

## 设计要点

- 命令可以产生两种结果：直接返回一条 `Message`（加入对话流），或提交一段渲染后的 prompt 回到 agent 循环
- `CommandRegistry` 是简单的地图结构，支持别名查找
- 内置命令通过工厂函数批量注册，保持 `command_registry.cpp` 可控

