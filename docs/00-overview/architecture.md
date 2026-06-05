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

## 分阶段重写路线

### 阶段 1：最小 agent loop

- CLI：`codeharness -p "prompt"`
- Provider：OpenAI-compatible streaming。
- Messages：`TextBlock`、`ToolUseBlock`、`ToolResultBlock`。
- Tools：`read_file`、`glob`、`grep`。
- Engine：工具调用回填和 max turns。
- Session：保存 JSON snapshot。

### 阶段 2：安全工具执行

- `bash`、`write_file`、`edit_file`。
- `PermissionChecker`。
- 敏感路径硬拒绝。
- 默认模式需要确认。
- 工具输出截断和 artifact 保存。

### 阶段 3：上下文系统

- system prompt builder。
- CLAUDE.md discovery。
- skills loader。
- memory store。
- slash commands。

### 阶段 4：MCP 和插件

- MCP stdio transport。
- MCP HTTP transport。
- plugin manifest、plugin skills、plugin commands、plugin MCP config。

### 阶段 5：交互和多 agent

- JSON Lines backend-only 协议。
- 复用或替换 React TUI。
- TaskManager。
- Subprocess subagent。
- Mailbox 和 team lifecycle。

### 阶段 6：coding agent refinement

- ohmo workspace。
- CLI / TUI runtime assembly。
- session resume 和 permission UX。
- coding-agent workflow polish。

## 初学者常见误区

- 不要把 LLM 调用写死到 engine 里。engine 应该只依赖统一的 `IStreamingClient`。
- 不要让工具直接修改全局状态。通过 `ToolExecutionContext` 传入 cwd、metadata、hook executor。
- 不要先做漂亮 TUI。没有稳定的 event stream 和 runtime，TUI 会拖慢进度。
- 不要跳过权限系统。agent 能执行 shell 命令后，安全边界就是核心功能。
- 不要让每个 provider 自己一套 message 结构。内部结构必须统一，provider 只做格式转换。
