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
2. `00-overview/feature-gap.md`：了解 CodeHarness 与 OpenHarness 的功能差异。
3. `00-overview/beginner-guide.md`：补齐 agent、streaming、JSON-RPC、进程、并发等基础概念。
4. `01-engine/cpp20-rewrite.md`：理解核心 agent loop 实现。
5. `15-provider/cpp20-rewrite.md`：理解模型 API、流式响应、工具调用格式转换。
6. `02-tools/cpp20-rewrite.md` 和 `05-permissions/cpp20-rewrite.md`：理解模型如何调用本地工具，以及如何加安全边界。
7. `08-mcp/cpp20-rewrite.md`：理解外部工具服务器接入。
8. `09-memory/cpp20-rewrite.md`、`03-skills/cpp20-rewrite.md`、`04-plugins/cpp20-rewrite.md`：理解上下文、记忆和扩展系统。
9. `14-ui/cpp20-rewrite.md`：最后理解交互式 TUI。

## 文档目录

```text
docs/
  00-overview/      总体架构、功能对比、学习路线
                    feature-gap.md — CodeHarness vs OpenHarness 功能差异
                    dependencies.md — 依赖选型
  01-engine/        Agent Loop、Engine 实现
  02-tools/         ToolRegistry、内置工具实现
  03-skills/        Skills 加载和注册
  04-plugins/       Plugin manifest 解析
  05-permissions/   权限模式、路径规则
  06-hooks/         生命周期 hook
  07-commands/      Slash command 注册
  08-mcp/           MCP client、stdio transport、JSON-RPC
  09-memory/        MEMORY.md、相关性搜索
  10-tasks/         后台任务、进程管理
  11-coordinator/   AgentDefinition、SubprocessBackend
  12-prompts/       System prompt 拼装
  13-config/        Settings、Credentials、Paths
  14-ui/            Native TUI、BackendHost
  15-provider/      OpenAI/Anthropic provider
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

## C++20 实现进度

五个阶段的核心重写已全部完成，共 153 个源文件、27 个测试文件：

| 阶段 | 模块 | 状态 |
| --- | --- | --- |
| 1. Agent Loop | CLI、Engine、Provider(OpenAI/Anthropic)、Messages、Sessions、Tools(read/glob/grep) | ✅ 已完成 |
| 2. Safe Tool Execution | bash/write/edit tools、PermissionChecker、敏感路径硬拒绝、输出截断 | ✅ 已完成 |
| 3. Context System | SystemPromptBuilder、AGENTS.md 发现、Skills、Memory、SlashCommands | ✅ 已完成 |
| 4. MCP & Plugins | MCP stdio transport、JSON-RPC、ClientSession、ToolAdapter、PluginLoader | ✅ 已完成 |
| 5. Interaction & Multi-Agent | TUI、BackendHost、TaskManager、Mailbox、TeamLifecycle、Coordinator | ✅ 已完成 |

实现代码见 `src/codeharness/`，测试见 `tests/`。

### 模块文件统计

| 目录 | 文件数 | 说明 |
| --- | --- | --- |
| tools | 26 | 工具注册和执行 |
| provider | 22 | OpenAI/Anthropic/Echo provider |
| core | 14 | 基础类型（Result、Message、Error 等） |
| coordinator | 10 | AgentDefinition、SubprocessBackend |
| mcp | 11 | MCP stdio transport、JSON-RPC |
| skills | 8 | SkillRegistry、SkillLoader |
| mailbox | 8 | 文件系统 mailbox、TeamLifecycle |
| config | 8 | Settings、Credentials、Paths |
| tui | 9 | Native TUI 渲染 |
| prompts | 4 | SystemPromptBuilder、ProjectContext |
| network | 4 | HTTP client、SSE parser |
| tasks | 4 | TaskManager、TaskTools |
| hooks | 6 | HookRegistry、HookExecutor |
| 其他 | 23 | CLI、commands、memory、permissions、plugins、runtime、sessions、engine、ui_backend |

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
| TUI | 已实现 native TUI（`tui/`），使用控制台原生渲染 |

## 重要原则

- 先实现 core loop，再实现外围生态。
- 所有 provider 都转换成同一个内部消息模型，不要让 engine 直接依赖 OpenAI 或 Anthropic 格式。
- 工具失败不能让程序崩溃，要变成 `ToolResultBlock{is_error=true}` 回给模型。
- 权限系统必须在工具执行前做，敏感路径不能被 `full_auto` 绕过。
- 网络 streaming、子进程输出、UI 事件都统一看成 event stream，方便调试和测试。
- 网络模块统一使用 standalone Asio；不要在 provider 或 MCP 中再引入 libcurl/Beast 作为第二套网络栈。
- 文档中的类名是建议，不要求一比一照搬；实现时优先保持小而清晰。
