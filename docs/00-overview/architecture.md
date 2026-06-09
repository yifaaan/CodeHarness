# OpenHarness 总体架构分析

OpenHarness 是一个 agent harness。LLM 负责推理和决定下一步，harness 负责把模型变成可以安全工作的 agent。

可以把它理解成：

```text
Agent Harness = 模型客户端 + 上下文拼装 + 工具系统 + 权限系统 + 记忆 + UI + 会话管理
```

模型本身不会读文件、不会跑命令、不会记住长期偏好，也不会知道本机有哪些安全边界。OpenHarness 把这些能力包在模型外面。

## 核心运行链路

一次普通 prompt 的流动如下：

```text
用户输入
  -> CLI 或 React TUI
  -> RuntimeBundle
  -> QueryEngine.submit_message
  -> run_query agent loop
  -> API client stream_message
  -> 模型返回文本 delta 或 tool_use
  -> ToolRegistry 查找工具
  -> Hooks + PermissionChecker
  -> tool.execute
  -> ToolResultBlock 回填为 user message
  -> 下一轮模型继续看工具结果
  -> 模型不再请求工具，本轮结束
```

这条链路最重要的点是：工具结果不是直接展示完就结束，而是会作为新的 `user` 消息追加到对话，让模型继续推理。

## 关键边界

OpenHarness 的设计边界很清晰，C++20 重写时应该保留这些边界。

| 边界 | 上游模块 | C++20 建议模块 |
| --- | --- | --- |
| CLI/TUI 到运行时 | `ui.app`、`ui.runtime`、`ui.backend_host` | `runtime`、`ui_backend` |
| 模型 provider 到内部消息 | `api.*` | `provider` |
| agent loop | `engine.query`、`engine.query_engine` | `engine` |
| 工具注册和执行 | `tools.*` | `tools` |
| 安全治理 | `permissions`、`hooks`、`sandbox` | `permissions`、`hooks`、`sandbox` |
| 长期上下文 | `memory`、`skills`、`prompts` | `memory`、`skills`、`prompts` |
| 外部工具服务器 | `mcp` | `mcp` |
| 多 agent | `tasks`、`swarm`、`coordinator` | `tasks`、`swarm` |

## RuntimeBundle 是什么

上游运行时会把一次会话需要的对象放到一个 bundle 里。这个 bundle 包含：

- `Settings`：模型、provider、权限、MCP、sandbox、memory 等配置。
- `ApiClient`：统一的 streaming 模型客户端。
- `ToolRegistry`：当前模型可调用的工具表。
- `PermissionChecker`：工具执行前的决策器。
- `HookExecutor`：生命周期扩展。
- `CommandRegistry`：处理 `/help`、`/memory`、`/tasks` 等命令。
- `QueryEngine`：真正执行 agent loop 的核心。
- `SessionBackend`：保存和恢复会话。
- `AppState`：供 UI 展示 model、cwd、permission mode、MCP 状态等。

C++20 里建议保留这个概念：

```cpp
struct RuntimeBundle {
    Settings settings;
    std::shared_ptr<IStreamingClient> apiClient;
    std::shared_ptr<ToolRegistry> tools;
    std::shared_ptr<PermissionChecker> permissions;
    std::shared_ptr<HookExecutor> hooks;
    std::shared_ptr<CommandRegistry> commands;
    std::unique_ptr<QueryEngine> engine;
    std::shared_ptr<SessionStore> sessions;
    AppState state;
};
```

初学者可以把 `RuntimeBundle` 看成“启动一次 agent 会话时打包好的工具箱”。

## Agent Loop 的本质

OpenHarness 的 agent loop 可以用伪代码表示：

```text
messages = history + user_prompt

while true:
    response = stream model(messages, tools, system_prompt)
    append assistant response to messages

    if response has no tool_use:
        break

    tool_results = []
    for each tool_use in response:
        result = execute_tool_safely(tool_use)
        tool_results.append(result)

    messages.append(user message containing tool_results)
```

为什么工具结果要作为 `user` 消息？因为模型 API 通常要求工具结果从“外部世界”返回给 assistant。assistant 发出工具调用后，下一条消息必须告诉它工具结果是什么。

## 事件流

OpenHarness 不直接让 engine 操作 UI，而是让 engine 产出事件：

- `AssistantTextDelta`：模型流式输出的一小段文本。
- `AssistantTurnComplete`：assistant 一轮消息结束。
- `ToolExecutionStarted`：工具开始执行。
- `ToolExecutionCompleted`：工具执行完成。
- `StatusEvent`：状态提示，例如重试、压缩。
- `ErrorEvent`：错误提示。
- `CompactProgressEvent`：上下文压缩进度。

这个设计对 C++20 很重要。你可以让 CLI、TUI、JSON 输出、测试都消费同一套事件，而不是在 engine 里写 UI 逻辑。

## 配置和扩展加载顺序

推荐 C++20 按下面顺序启动：

1. 解析 CLI 参数。
2. 找到配置目录和数据目录。
3. 读取 settings 文件和环境变量。
4. 合并 CLI override。
5. 解析 provider profile 和认证信息。
6. 加载 skills、plugins、commands、hooks。
7. 连接 MCP server。
8. 注册内置工具和 MCP 工具。
9. 构建 system prompt。
10. 创建 QueryEngine。
11. 进入 print mode 或 TUI mode。

## 推荐 C++20 总体模块

```text
src/codeharness/
  core/          Result、Error、Json、Path、EventQueue
  config/        Settings、Profile、ConfigLoader、PathResolver
  provider/      IStreamingClient、AnthropicClient、OpenAIClient
  engine/        QueryEngine、QueryLoop、Conversation、StreamEvent
  tools/         ITool、ToolRegistry、BuiltinTools、ToolExecutor
  permissions/   PermissionChecker、PathRules、CommandRules
  hooks/         HookRegistry、HookExecutor、HookEvent
  prompts/       SystemPromptBuilder、EnvironmentDetector
  sessions/      SessionStore、SessionSnapshot
  skills/        SkillLoader、SkillRegistry
  plugins/       PluginLoader、PluginManifest
  mcp/           McpClientManager、JsonRpc、StdioTransport、HttpTransport
  tasks/         TaskManager、ProcessRunner
  swarm/         Mailbox、TeamStore、SubprocessBackend
  ui/            BackendHost、NativeTui optional
  cli/           main command line
```

## 分阶段重写进度

五个阶段的核心重写已全部完成（共 153 个源文件、27 个测试文件）。实现代码见 `src/codeharness/`。

### 阶段 1：最小 agent loop ✅ 已完成

- CLI：`cli/` — `run_cli()` 参数解析（CLI11）
- Provider：`provider/` — `EchoProvider`、`OpenAIProvider`、`AnthropicProvider`，统一 `Provider` 接口，SSE streaming
- Messages：`core/message.h` — `TextBlock`、`ToolUseBlock`、`ToolResultBlock`、`ContentBlock` variant
- Tools：`read_file`、`glob`、`grep`，通过 `ToolRegistry` 注册和查找
- Engine：`engine/engine.h/.cpp` — `Engine::run_streaming()`，工具调用回填和 max turns，`EngineEvent` variant
- Session：`sessions/session_store.h/.cpp` — JSON 保存和恢复，Markdown 导出

### 阶段 2：安全工具执行 ✅ 已完成

- `bash` + `reproc`、`write_file` + atomic write、`edit_file` + old/new string replace
- `PermissionChecker`：`PermissionMode::{Default, Plan, FullAuto}`、路径规则、命令规则
- 敏感路径硬拒绝（`.ssh`、`.aws/credentials`、`.kube/config` 等），不可被 `full_auto` 绕过
- 默认模式写操作需要确认，read-only 工具自动允许
- 工具输出截断 + artifact 保存策略

### 阶段 3：上下文系统 ✅ 已完成

- `prompts/system_prompt.h/.cpp` — `build_system_prompt()` + `EnvironmentDetector`
- `prompts/project_context.h/.cpp` — `load_project_context_files()` 向上搜索 AGENTS.md/CLAUDE.md
- `skills/` — `SkillRegistry`、`SkillTool`、bundled/用户/项目技能加载
- `memory/memory_store.h/.cpp` — Markdown memory、相关性搜索、使用索引
- `commands/command_registry.h/.cpp` — `/help`、`/skills`、`/model`、`/clear` 等 slash command

### 阶段 4：MCP 和插件 ✅ 已完成

- MCP stdio transport：`mcp/stdio_transport.h/.cpp`（基于 `reproc`）
- MCP JSON-RPC：`mcp/json_rpc.h/.cpp`
- MCP ClientSession：`mcp/client_session.h/.cpp` — initialize、list_tools、call_tool
- MCP ToolAdapter：`mcp/tool_adapter.h/.cpp` — MCP 工具包装为标准 `Tool`
- Plugin：`plugins/plugin_loader.h/.cpp` — `plugin.json` 解析、user/project 插件加载

### 阶段 5：交互和多 agent ✅ 已完成

- TUI：`tui/` — TuiAppModel、command palette、model selector、permission 弹窗、Markdown 渲染
- BackendHost：`ui_backend/ui_backend.h/.cpp` — JSON Lines OHJSON: 协议
- TaskManager：`tasks/task_manager.h/.cpp` — shell/agent task 创建、状态落盘、stop、tail
- Task tools + Agent tool：`tasks/task_tools.h/.cpp`，已接入 ToolRegistry
- Mailbox + TeamLifecycle：`mailbox/` — 文件系统 mailbox、`send_message` 工具、team CRUD
- Coordinator：`coordinator/` — AgentDefinition 加载、SubprocessBackend::spawn
- Runtime：`runtime/runtime.h/.cpp` — `RuntimeBundle` 组装层

---

## 后续可能扩展

以下功能不在当前 C++ 实现范围内，可根据需求后续添加：

- MCP HTTP transport
- Provider：GitHub Copilot、Codex subscription
- `write_to_task` stdin 写入
- 自动上下文压缩（Auto-compact）
- 并发工具执行
- ohmo workspace 层

## 初学者常见误区

- 不要把 LLM 调用写死到 engine 里。engine 应该只依赖统一的 `IStreamingClient`。
- 不要让工具直接修改全局状态。通过 `ToolExecutionContext` 传入 cwd、metadata、hook executor。
- 不要先做漂亮 TUI。没有稳定的 event stream 和 runtime，TUI 会拖慢进度。
- 不要跳过权限系统。agent 能执行 shell 命令后，安全边界就是核心功能。
- 不要让每个 provider 自己一套 message 结构。内部结构必须统一，provider 只做格式转换。
