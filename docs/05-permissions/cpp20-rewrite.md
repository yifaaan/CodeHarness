# Permissions C++20 重写方案

Permissions 模块决定工具调用是否允许执行。

上游关键文件：

- `docs/OpenHarness/src/openharness/permissions/modes.py`
- `docs/OpenHarness/src/openharness/permissions/checker.py`
- `docs/OpenHarness/src/openharness/config/settings.py`
- `docs/OpenHarness/src/openharness/ui/permission_dialog.py`
- `docs/OpenHarness/src/openharness/swarm/permission_sync.py`

## 权限模式

OpenHarness 有三种基础模式：

| 模式 | 行为 |
| --- | --- |
| `default` | 只读工具自动允许，写文件和执行命令需要确认 |
| `plan` | 阻止修改类工具，只允许规划和读取 |
| `full_auto` | 默认允许工具执行，但敏感路径仍硬拒绝 |

注意：`full_auto` 不是完全无安全限制。敏感路径永远不能被绕过。

## 配置结构

```cpp
enum class PermissionMode {
    Default,
    Plan,
    FullAuto
};

struct PathRule {
    std::string pattern;
    bool allow = true;
};

struct PermissionSettings {
    PermissionMode mode = PermissionMode::Default;
    std::vector<std::string> allowedTools;
    std::vector<std::string> deniedTools;
    std::vector<PathRule> pathRules;
    std::vector<std::string> deniedCommands;
};
```

## 决策结果

```cpp
struct PermissionDecision {
    bool allowed = false;
    bool requiresConfirmation = false;
    std::string reason;
};
```

语义：

- `allowed=true, requiresConfirmation=false`：直接执行。
- `allowed=false, requiresConfirmation=true`：需要问用户。
- `allowed=false, requiresConfirmation=false`：拒绝执行。

## PermissionChecker

```cpp
class PermissionChecker {
public:
    explicit PermissionChecker(PermissionSettings settings);

    PermissionDecision evaluate(std::string_view toolName,
                                bool isReadOnly,
                                std::optional<std::filesystem::path> targetPath,
                                std::optional<std::string> command) const;

private:
    PermissionSettings settings_;
};
```

## 决策顺序

顺序非常重要。建议按上游顺序：

1. 敏感路径硬拒绝。
2. `denied_tools`。
3. `allowed_tools`。
4. path deny rules。
5. denied command patterns。
6. `full_auto` 允许。
7. 只读工具允许。
8. `plan` 模式阻止写操作。
9. `default` 模式下写操作需要确认。

为什么敏感路径最先？因为不能让用户配置中的 allowed tool 或 full_auto 绕过系统安全底线。

## 敏感路径

建议内置拒绝：

```text
~/.ssh/*
~/.aws/credentials
~/.aws/config
~/.config/gcloud/*
~/.azure/*
~/.gnupg/*
~/.docker/config.json
~/.kube/config
~/.openharness/credentials.json
~/.openharness/copilot_auth.json
```

还要考虑 Windows：

```text
%USERPROFILE%\.ssh\*
%USERPROFILE%\.aws\credentials
```

路径检查必须使用 canonical path，防止 symlink 绕过。

## 命令拒绝规则

命令匹配可以先用简单 wildcard 或 regex。建议内置一些保护：

- `rm -rf /`
- `del /s /q C:\`
- `format *`
- `DROP DATABASE`
- `curl ... | sh` 可以不硬拒绝，但应至少要求确认。

不要试图用字符串规则完全解决 shell 安全。真正安全要靠 sandbox、确认和最小权限。

## UI 确认流程

ToolExecutor 中的流程：

```text
decision = checker.evaluate(...)

if rejected:
    return error tool result

if requires confirmation:
    allowed = permissionPrompt(toolName, reason)
    if not allowed:
        return error tool result

execute tool
```

权限拒绝也要返回 tool result，让模型知道动作没有执行。

## Edit approval

写文件和 edit 可以支持更细粒度确认：

- `once`：本次允许。
- `always`：本会话对同类操作允许。
- `reject`：拒绝。

第一版可以先只做 yes/no。

## Swarm 权限同步

多 agent 场景中 worker 不能直接弹窗问用户。上游做法是 worker 向 leader 发权限请求，leader 统一判断和确认。

C++ 第一版可以延后 swarm 权限同步，但接口应预留：

```cpp
class IPermissionPrompt {
public:
    virtual bool ask(std::string_view tool, std::string_view reason) = 0;
};
```

本地 UI 是一种 prompt，swarm leader 代理也是一种 prompt。

## 测试清单

- `full_auto` 允许普通写操作。
- `full_auto` 仍拒绝 `.ssh/id_rsa`。
- `plan` 阻止 `write_file`。
- `default` 对 `read_file` 自动允许。
- `default` 对 `bash` 需要确认。
- `denied_tools` 优先于 `allowed_tools`。
- path rule 能拒绝特定目录。
- symlink 指向敏感路径被拒绝。
