# CodeHarness C++20 重写文档索引

本目录用于记录对上游 `HKUDS/OpenHarness` 的结构分析，以及将其用 C++20 重新实现为 CodeHarness 的设计方案。

上游源码已经作为 git submodule 放在：

```text
docs/OpenHarness
```

当前 submodule 指向上游 `main` 分支提交：`bf5931e7c12c6b2f82bbfc3b5ed2f82a49319b3a`。

## 阅读顺序

建议按下面顺序阅读。你是 agent 开发和网络编程初学者时，不要直接从 MCP、swarm 或 UI 开始。

1. `00-overview/architecture.md`：先理解 OpenHarness 是什么，以及一次 agent 对话如何流动。
2. `00-overview/beginner-guide.md`：补齐 agent、streaming、JSON-RPC、进程、并发等基础概念。
3. `01-engine/design.md`：理解核心 agent loop。
4. `15-provider/cpp20-rewrite.md`：理解模型 API、流式响应、工具调用格式转换。
5. `02-tools/design.md` 和 `05-permissions/cpp20-rewrite.md`：理解模型如何调用本地工具，以及如何加安全边界。
6. `08-mcp/cpp20-rewrite.md`：理解外部工具服务器接入。
7. `09-memory/cpp20-rewrite.md`、`03-skills/cpp20-rewrite.md`、`04-plugins/cpp20-rewrite.md`：理解上下文、记忆和扩展系统。
8. `14-ui/cpp20-rewrite.md`：最后理解交互式 TUI 和可选 ohmo workspace。

## 文档目录

```text
docs/
  00-overview/      总体架构、学习路线、分阶段重写路线
                    dependency 选型见 dependencies.md
  01-engine/        Agent Loop、QueryEngine、stream event
  02-tools/         ToolRegistry、内置工具、工具结果管理
  03-skills/        Markdown skills 加载和 C++20 重写
  04-plugins/       插件 manifest、commands、hooks、MCP 配置
  05-permissions/   权限模式、路径规则、敏感路径保护
  06-hooks/         生命周期 hook 和拦截机制
  07-commands/      Slash command 注册和执行
  08-mcp/           MCP client、stdio/http transport、JSON-RPC
  09-memory/        MEMORY.md、相关性搜索、usage index
  10-tasks/         后台任务、进程管理、日志
  11-coordinator/   多 agent、swarm、mailbox、team lifecycle
  12-prompts/       system prompt 拼装、CLAUDE.md、环境注入
  13-config/        Settings、profile、路径、持久化
  14-ui/            React TUI 协议、backend-only、ohmo workspace
  15-provider/      Anthropic/OpenAI/Copilot/Codex provider 适配
  OpenHarness/      上游源码 submodule
```

## 上游模块速览

OpenHarness 的核心目录是 `docs/OpenHarness/src/openharness`：

| 模块 | 作用 |
| --- | --- |
| `engine` | 核心 agent loop，负责模型 streaming、工具调用、工具结果回填 |
| `api` | Anthropic、OpenAI-compatible、Copilot、Codex 等模型 provider 适配 |
| `tools` | 文件、shell、搜索、MCP、任务、多 agent 等工具 |
| `permissions` | 默认、计划、自动模式，路径和命令安全规则 |
| `hooks` | pre/post tool、session start/end、notification 等生命周期扩展 |
| `mcp` | Model Context Protocol client，连接外部工具服务器 |
| `skills` | Markdown 技能加载，类似按需知识包 |
| `plugins` | 插件目录加载，贡献 skills、commands、hooks、agents、MCP |
| `commands` | `/help`、`/memory`、`/tasks` 等 slash command |
| `memory` | Markdown 长期记忆、索引、搜索、usage 统计 |
| `tasks` | 本地后台 shell/agent 任务管理 |
| `swarm` | 多 agent 后端、mailbox、team、worktree 隔离 |
| `coordinator` | coordinator prompt 和 agent definition 加载 |
| `prompts` | system prompt、环境信息、CLAUDE.md、memory 拼装 |
| `config` | settings、profile、路径、权限、sandbox、MCP 配置 |
| `ui` | React TUI 后端协议、runtime 装配、print mode |

## C++20 重写目标

第一版不要追求完全复刻所有功能。建议先做一个可运行、可测试、边界清晰的核心版本：

1. 支持 `codeharness -p "prompt"` 非交互模式。
2. 支持 OpenAI-compatible 和 Anthropic-compatible streaming。
3. 支持内部统一消息模型和工具调用模型。
4. 支持 3 到 6 个基础工具：`read_file`、`write_file`、`edit_file`、`grep`、`glob`、`bash`。
5. 支持权限确认、敏感路径硬拒绝、max turns。
6. 支持 JSON session 保存和恢复。
7. 后续再加入 TUI、subagent、ohmo。

## 当前 C++ 实现进度

截至 2026-06-04，本仓库已实现并测试的 C++ 模块包括：

| 模块 | 当前状态 |
| --- | --- |
| engine/provider/tools/permissions/hooks | 已有基础实现和测试，支持工具回填、权限判定和 hook 拦截 |
| skills/plugins/commands/prompts/memory/MCP | 已有 C++ 骨架与 focused tests，可作为后续 runtime 组装材料 |
| tasks | 已实现 `TaskManager` v1、一次性 `local_agent`、`task_*` 工具和最小 `agent` 工具：后台 shell/agent task、JSON 状态、log、tail、stop、ToolRegistry 接入；stop 采用 request-stop 机制，避免 `reproc::process` 跨线程并发访问 |
| mailbox/coordinator | 已实现文件系统 Mailbox、`send_message` 工具、TeamLifecycleManager v1、AgentDefinitionLoader v1、AgentDefinitionRegistry v1，以及 SubprocessBackend::spawn 最小版（创建 `local_agent` task 并记录 team membership） |
| swarm/UI | 仍处设计阶段；下一步依赖 worker 消息消费、spawn config 与 agent definition 合并逻辑、backend-only UI 协议继续推进 |

与上游 `docs/OpenHarness` 对比，Mailbox、`send_message`、TeamLifecycleManager、AgentDefinitionLoader、AgentDefinitionRegistry 和 SubprocessBackend::spawn 已进入 C++ 第一版；当前最自然的后续实现顺序是：worker 消息消费、spawn config 与 agent definition 合并逻辑、backend-only UI 协议。

## 推荐依赖

使用 CMake + vcpkg manifest 作为构建入口时，推荐先选少量稳定依赖。外部库确认 vcpkg 中存在对应 port：

| 领域 | 推荐 |
| --- | --- |
| JSON | `nlohmann-json` |
| HTTP/SSE | `asio` + `openssl` + `ada`，HTTP framing 和 SSE parser 先放在项目内实现 |
| HTTP 解压 | `zlib` + `brotli` |
| 搜索 | `re2` 通过 vcpkg 导入，`std::filesystem` 负责遍历 |
| 进程 | `reproc`，统一包成 `ProcessRunner` |
| 异步 | 初版 thread pool + blocking queue，网络 I/O 统一走 standalone Asio |
| YAML frontmatter | `yaml-cpp` |
| 日志 | `spdlog` |
| CLI | `CLI11` |
| Result 类型 | `expected-lite`，用户确认可作为 awesome-cpp 约束外的例外导入 |
| 本地索引 | `sqlite3` |
| 测试 | `doctest` |
| TUI | 初版先不做，后续考虑 `FTXUI` |

## 重要原则

- 先实现 core loop，再实现外围生态。
- 所有 provider 都转换成同一个内部消息模型，不要让 engine 直接依赖 OpenAI 或 Anthropic 格式。
- 工具失败不能让程序崩溃，要变成 `ToolResultBlock{is_error=true}` 回给模型。
- 权限系统必须在工具执行前做，敏感路径不能被 `full_auto` 绕过。
- 网络 streaming、子进程输出、UI 事件都统一看成 event stream，方便调试和测试。
- 网络模块统一使用 standalone Asio；不要在 provider 或 MCP 中再引入 libcurl/Beast 作为第二套网络栈。
- 文档中的类名是建议，不要求一比一照搬；实现时优先保持小而清晰。
