# UI and ohmo C++20 rewrite plan

The UI layer presents engine events to the user and sends user input,
permission decisions, and selection responses back to the runtime.

Upstream reference files:

- `docs/OpenHarness/src/openharness/ui/protocol.py`
- `docs/OpenHarness/src/openharness/ui/backend_host.py`
- `docs/OpenHarness/src/openharness/ui/runtime.py`
- `docs/OpenHarness/src/openharness/ui/react_launcher.py`
- `docs/OpenHarness/frontend/terminal/src/*`
- `docs/OpenHarness/ohmo/*`

## UI architecture

The upstream default is a two-process shape:

```text
React Ink TUI (Node.js)
  <-> stdin/stdout JSON Lines
Python backend host
  -> RuntimeBundle
  -> QueryEngine
```

Backend events use this prefix:

```text
OHJSON:{...json...}
```

Frontend requests are plain JSON lines.

## Recommended C++ route

Do not rewrite the full TUI first. Keep the first step focused on the
coding-agent runtime path.

### Phase A: backend-only protocol

Implement:

```text
codeharness --backend-only
```

It reads JSON requests from stdin and writes `OHJSON:` events to stdout. This
lets the existing React TUI exercise the C++ runtime before native UI work.

### Phase B: native TUI

After the runtime and event stream stabilize, implement a native TUI. The
native TUI should consume the same internal event model as backend-only mode.

## FrontendRequest

Common upstream request types:

- `submit_line`
- `permission_response`
- `question_response`
- `list_sessions`
- `select_command`
- `apply_select_command`
- `interrupt`
- `shutdown`

C++ shape:

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

Common events:

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

`emit()` writes:

```text
OHJSON:{json}\n
```

## Permission modal

When a tool requires confirmation, the backend emits:

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

Frontend response:

```json
{"type":"permission_response","request_id":"...","allowed":true}
```

The C++ backend can keep a pending map while waiting for responses:

```cpp
std::unordered_map<std::string, std::promise<bool>> pendingPermissions;
```

Permission prompts should be serialized so multiple prompts do not appear at
the same time.

## ohmo personal agent

ohmo is an optional personal-agent layer on top of the coding-agent runtime.
For C++, keep it focused on local workspace composition:

- `~/.ohmo` workspace.
- `soul.md`, `identity.md`, and `user.md`.
- personal memory.
- a reusable runtime configuration for the local coding agent.

Possible future commands:

```text
codeharness-ohmo init
codeharness-ohmo
```

Do not implement full ohmo first. Make the core runtime reusable, then let ohmo
inject a different workspace, prompt, and memory backend.

## Implementation status

### Phase A: backend-only protocol ✅

`codeharness --backend-only` is implemented in `src/codeharness/ui_backend/`:

- `BackendHost` 类 — 读取 stdin JSON Lines，输出 `OHJSON:` 前缀事件到 stdout
- `FrontendRequest` / `BackendEvent` 类型定义
- 支持 `submit_line`、`permission_response`、`question_response`、`interrupt`、`shutdown`
- 权限弹窗通过 `modal_request`/`permission_response` 协议实现
- Pending map 处理异步权限确认

### Phase B: native TUI ✅

Native TUI 已实现（`src/codeharness/tui/`，10 个文件）：

- `TuiAppModel` / `run_tui()` — 状态管理
- Command palette、model selector、question modal、permission 弹窗
- Markdown 渲染
- Paste burst detection
- 与 Engine 共享相同的事件模型

### Remaining work 📋

1. **Session list and resume** in TUI
2. **Native TUI polish** — hotkeys、color theme、split pane
3. **ohmo workspace layer** — `~/.ohmo` workspace、soul.md/identity.md

## Test checklist

- Receiving `submit_line` emits transcript, assistant delta, and line complete. ✅
- Permission modal request/response unblocks tool execution. ✅
- Shutdown saves the session and exits. ✅
- Partial JSON Lines input and invalid JSON return structured errors. ✅
- Session list/resume uses the same runtime/session model as CLI. 📋
