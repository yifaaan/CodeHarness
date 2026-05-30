# Commands C++20 重写方案

Slash command 是用户输入 `/xxx` 时触发的命令系统。它和模型工具不同：工具由模型调用，slash command 由用户直接调用。

上游关键文件：

- `docs/OpenHarness/src/openharness/commands/registry.py`
- `docs/OpenHarness/src/openharness/ui/runtime.py`
- `docs/OpenHarness/src/openharness/skills/registry.py`
- `docs/OpenHarness/src/openharness/plugins/loader.py`

## CommandRegistry

```cpp
struct CommandResult {
    std::optional<std::string> message;
    bool shouldExit = false;
    bool clearScreen = false;
    bool refreshRuntime = false;
    std::optional<std::string> submitPrompt;
    std::optional<std::string> submitModel;
};

struct CommandContext {
    RuntimeBundle& runtime;
    std::filesystem::path cwd;
};

using CommandHandler = std::function<CommandResult(std::string_view args,
                                                   CommandContext& ctx)>;

struct SlashCommand {
    std::string name;
    std::string description;
    CommandHandler handler;
    std::vector<std::string> aliases;
    bool remoteInvocable = true;
    bool remoteAdminOptIn = false;
};

class CommandRegistry {
public:
    void registerCommand(SlashCommand command);
    const SlashCommand* lookup(std::string_view input) const;
    std::vector<SlashCommand> list() const;
};
```

## 用户输入分流

Runtime 处理用户输入：

```text
if line starts with "/":
    command = registry.lookup(line)
    if found:
        result = command.handler(args)
        handle CommandResult
    else if skill slash command exists:
        build skill prompt
    else:
        show unknown command
else:
    QueryEngine.submitMessage(line)
```

## 常见内置命令

第一版建议支持：

- `/help`：列出命令。
- `/exit`：退出。
- `/clear`：清空会话。
- `/model`：显示或切换模型。
- `/permissions`：显示或切换权限模式。
- `/skills`：列出 skills。
- `/memory`：记忆管理，后续实现。
- `/tasks`：后台任务，后续实现。
- `/mcp`：MCP 状态，后续实现。

## Skill command

`userInvocable` skill 可以注册为 command。处理逻辑：

```text
/review src/main.cpp
  -> find skill review
  -> prompt = skill.content with $ARGUMENTS replaced
  -> return CommandResult{submitPrompt=prompt}
```

## Plugin command

Plugin command 与 skill command 类似，但命名带 namespace。

```text
/git:commit
/security:review
```

## Remote command 安全

ohmo gateway 允许远程聊天触发命令，但有安全限制。C++ 设计里建议命令带两个字段：

- `remoteInvocable`：远程是否可调用。
- `remoteAdminOptIn`：是否需要管理员配置显式允许。

高风险命令默认禁止远程调用：

- `/resume`
- `/diff`
- `/commit`
- `/tasks`
- `/config`
- `/bridge`

## 测试清单

- `/help` 能列出命令。
- alias 能命中。
- 未知命令返回错误。
- command 能返回 `submitPrompt` 并进入 QueryEngine。
- remote 模式拒绝本地敏感命令。
- skill slash command 能替换参数。
