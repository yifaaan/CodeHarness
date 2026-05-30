# Hooks C++20 重写方案

Hooks 是生命周期扩展机制。它允许用户或插件在关键事件发生时执行命令、HTTP 请求或模型判断。

上游关键文件：

- `docs/OpenHarness/src/openharness/hooks/events.py`
- `docs/OpenHarness/src/openharness/hooks/schemas.py`
- `docs/OpenHarness/src/openharness/hooks/loader.py`
- `docs/OpenHarness/src/openharness/hooks/executor.py`
- `docs/OpenHarness/src/openharness/hooks/hot_reload.py`

## Hook 事件

建议 C++20 保留这些事件：

```cpp
enum class HookEvent {
    SessionStart,
    SessionEnd,
    PreCompact,
    PostCompact,
    PreToolUse,
    PostToolUse,
    UserPromptSubmit,
    Notification,
    Stop,
    SubagentStop
};
```

最重要的是：

- `PreToolUse`：工具执行前，可阻止。
- `PostToolUse`：工具执行后，记录或通知。
- `UserPromptSubmit`：用户提交 prompt 后。
- `SessionStart` / `SessionEnd`：会话生命周期。

## Hook 类型

上游支持：

| 类型 | 行为 |
| --- | --- |
| `command` | 执行 shell 命令 |
| `http` | POST JSON 到 URL |
| `prompt` | 调用模型判断是否通过 |
| `agent` | 类似 prompt，但语义上是更彻底的 agent 检查 |

C++ 第一版建议先支持 `command` 和 `http`。`prompt` / `agent` 可以等 provider 稳定后实现。

## HookDefinition

```cpp
enum class HookType {
    Command,
    Http,
    Prompt,
    Agent
};

struct HookDefinition {
    HookEvent event;
    HookType type;
    int priority = 0;
    std::optional<std::string> matcher;
    bool blockOnFailure = false;
    int timeoutSeconds = 30;
    nlohmann::json config;
};
```

`matcher` 用于限制 hook 只匹配某些工具或场景。例如只对 `bash` 触发。

## HookRegistry

```cpp
class HookRegistry {
public:
    void add(HookDefinition hook);
    std::vector<HookDefinition> get(HookEvent event) const;

private:
    std::unordered_map<HookEvent, std::vector<HookDefinition>> hooks_;
};
```

取出时要按 priority 降序排序，同 priority 保持注册顺序。

## HookExecutor

```cpp
struct HookResult {
    bool success = true;
    bool blocked = false;
    std::string output;
    std::string reason;
};

struct HookExecutionResult {
    bool blocked = false;
    std::string reason;
    std::vector<HookResult> results;
};

class HookExecutor {
public:
    HookExecutionResult execute(HookEvent event,
                                const nlohmann::json& payload);
};
```

如果 `PreToolUse` 的 hook 返回 blocked，ToolExecutor 应停止工具执行并返回 error tool result。

## Command Hook

Command hook 需要特别注意安全和转义。

配置示例：

```json
{
  "event": "pre_tool_use",
  "type": "command",
  "command": "python scripts/check_tool.py $ARGUMENTS",
  "block_on_failure": true,
  "timeout_seconds": 10
}
```

实现建议：

- 把 hook payload 序列化为 JSON。
- 通过环境变量传给命令，例如 `OPENHARNESS_HOOK_PAYLOAD`。
- 不建议直接字符串替换 `$ARGUMENTS`，除非做严格 shell escape。
- 设置 timeout。
- 捕获 stdout/stderr。

第一版可以支持：

```text
command receives payload through stdin
```

这样比拼 shell 字符串安全。

## HTTP Hook

HTTP hook 做 POST：

```json
{
  "event": "post_tool_use",
  "type": "http",
  "url": "https://example.com/hook"
}
```

实现要点：

- 只允许 `http` 和 `https`。
- 设置 timeout。
- 不自动带上本地凭据。
- 2xx 认为成功，其他认为失败。
- 如果 `blockOnFailure` 为 true，则失败可阻止后续流程。

## Prompt/Agent Hook

这类 hook 会调用模型，让模型判断 payload 是否通过。建议返回严格 JSON：

```json
{"ok": true}
```

或：

```json
{"ok": false, "reason": "command deletes build output"}
```

C++ 第一版可以先不做，避免 hook 与 provider 互相依赖导致复杂启动顺序。

## 加载顺序

Hook 来源：

1. settings 中显式配置。
2. enabled plugins 中的 hooks。

Project plugin 默认应关闭，防止打开陌生仓库就执行恶意 hook。

## 热重载

上游有 hot reload。C++ 第一版可以不做。后续可用：

- 文件 mtime 轮询。
- 平台文件监控 API。
- 手动 `/reload-hooks` 命令。

## 测试清单

- priority 降序执行。
- 同 priority 保持注册顺序。
- matcher 不匹配时不执行。
- command hook timeout。
- blockOnFailure 能阻止工具。
- non-blocking hook 失败只记录，不阻止。
- HTTP hook 非 2xx 处理正确。
