# UI、ohmo、Gateway C++20 重写方案

UI 模块负责把 engine 事件展示给用户，并把用户输入、权限确认、选择弹窗传回 runtime。

上游关键文件：

- `docs/OpenHarness/src/openharness/ui/protocol.py`
- `docs/OpenHarness/src/openharness/ui/backend_host.py`
- `docs/OpenHarness/src/openharness/ui/runtime.py`
- `docs/OpenHarness/src/openharness/ui/react_launcher.py`
- `docs/OpenHarness/frontend/terminal/src/*`
- `docs/OpenHarness/ohmo/*`
- `docs/OpenHarness/ohmo/gateway/*`
- `docs/OpenHarness/src/openharness/channels/*`

## UI 架构

上游默认是双进程：

```text
React Ink TUI (Node.js)
  <-> stdin/stdout JSON Lines
Python backend host
  -> RuntimeBundle
  -> QueryEngine
```

后端输出事件前缀：

```text
OHJSON:{...json...}
```

前端发送普通 JSON line。

## C++ 建议路线

不要第一版重写完整 TUI。建议分两步：

### 阶段 A：backend-only 兼容协议

实现：

```text
codeharness --backend-only
```

它从 stdin 读 JSON request，从 stdout 写 `OHJSON:` event。这样可以复用上游 React TUI，先验证 C++ runtime。

### 阶段 B：原生 TUI

核心稳定后再用 `FTXUI` 或自研 ANSI renderer 做原生 TUI。

原生 TUI 只需要消费同一套内部 `StreamEvent`。

## FrontendRequest

上游请求类型：

- `submit_line`
- `permission_response`
- `question_response`
- `list_sessions`
- `select_command`
- `apply_select_command`
- `interrupt`
- `shutdown`

C++ 结构：

```cpp
struct FrontendRequest {
    std::string type;
    std::optional<std::string> line;
    std::optional<std::string> command;
    std::optional<std::string> value;
    std::optional<std::string> requestId;
    std::optional<bool> allowed;
    std::optional<std::string> answer;
    std::vector<ImageAttachment> images;
};
```

## BackendEvent

常见事件：

- `ready`
- `state_snapshot`
- `tasks_snapshot`
- `transcript_item`
- `assistant_delta`
- `assistant_complete`
- `line_complete`
- `tool_started`
- `tool_completed`
- `modal_request`
- `select_request`
- `todo_update`
- `plan_mode_change`
- `swarm_status`
- `error`
- `shutdown`

## BackendHost

```cpp
class BackendHost {
public:
    BackendHost(RuntimeBundle runtime,
                std::istream& input,
                std::ostream& output);

    void run();

private:
    void handleRequest(const FrontendRequest& request);
    void emit(const BackendEvent& event);
};
```

`emit()` 输出：

```text
OHJSON:{json}\n
```

## 权限弹窗

工具需要确认时，后端发：

```json
{
  "type": "modal_request",
  "modal": {
    "kind": "permission",
    "request_id": "...",
    "tool_name": "bash",
    "reason": "Execute shell command"
  }
}
```

前端回：

```json
{"type":"permission_response","request_id":"...","allowed":true}
```

C++ 后端需要用 pending map 等待 response：

```cpp
std::unordered_map<std::string, std::promise<bool>> pendingPermissions;
```

权限弹窗应串行化，避免多个弹窗同时出现。

## ohmo personal agent

ohmo 是基于 OpenHarness runtime 的个人 agent 应用，主要增加：

- `~/.ohmo` workspace。
- `soul.md`、`identity.md`、`user.md`。
- personal memory。
- gateway 配置。
- IM channel 接入。

C++ 后续可以做：

```text
codeharness-ohmo init
codeharness-ohmo
codeharness-ohmo gateway run
```

不要一开始实现完整 ohmo。先把核心 runtime 做成可复用，再给 ohmo 注入不同 workspace、prompt、memory backend。

## Gateway 架构

Gateway 用于把 Telegram、Slack、Discord、Feishu 等消息接入 agent。

上游结构：

```text
Channel adapter
  -> MessageBus.inbound
  -> GatewayBridge
  -> SessionRuntimePool
  -> RuntimeBundle / QueryEngine
  -> MessageBus.outbound
  -> ChannelManager
  -> channel.send
```

C++ 结构：

```cpp
struct InboundMessage {
    std::string channel;
    std::string senderId;
    std::string chatId;
    std::string content;
    nlohmann::json metadata;
};

struct OutboundMessage {
    std::string channel;
    std::string chatId;
    std::string content;
    nlohmann::json metadata;
};

class MessageBus {
public:
    BlockingQueue<InboundMessage> inbound;
    BlockingQueue<OutboundMessage> outbound;
};
```

## Session key 隔离

远程聊天必须防止不同用户共享上下文。

规则建议：

- 私聊：`channel:chat_id`
- 群聊：`channel:chat_id:sender_id`
- 群聊 thread：`channel:chat_id:thread_id:sender_id`

这样同一个群里的不同用户不会共享 agent session。

## Channels

C++ 直接实现所有 IM SDK 成本高。建议：

1. 第一版 gateway 只做本地 HTTP/WebSocket 或 stdio adapter 协议。
2. 具体 Telegram/Slack/Feishu adapter 可以用外部进程实现。
3. C++ core 只处理统一 `InboundMessage` / `OutboundMessage`。

如果实现网络 adapter，按你的约束也应统一使用 standalone Asio。

## Remote command 安全

远程消息默认不能执行本地高风险 slash commands。

建议配置：

```json
{
  "allow_remote_admin_commands": false,
  "allowed_remote_admin_commands": []
}
```

命令本身也要声明：

- `remoteInvocable`
- `remoteAdminOptIn`

## 第一版路线

1. 实现 `--backend-only` JSON Lines 协议。
2. Fake backend 测试 React TUI 协议。
3. 接入真实 RuntimeBundle。
4. 支持 permission modal。
5. 支持 select command。
6. 支持 session list/resume。
7. 后续实现 FTXUI 原生 UI。
8. 最后实现 ohmo workspace 和 gateway。

## 测试清单

- 收到 `submit_line` 后输出 transcript、assistant delta、line complete。
- permission modal request/response 能 unblock 工具。
- shutdown 能保存 session 并退出。
- JSON Lines 半包或无效 JSON 有错误处理。
- remote command 安全策略生效。
