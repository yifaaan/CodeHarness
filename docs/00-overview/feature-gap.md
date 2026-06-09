# CodeHarness vs OpenHarness 功能对比

本文档记录 OpenHarness 中存在但 CodeHarness C++ 实现中暂未移植的功能，作为后续扩展参考。

## 工具对比

| OpenHarness 工具 | CodeHarness 状态 | 说明 |
| --- | --- | --- |
| `bash` | ✅ 已实现 | 基于 reproc |
| `read_file` | ✅ 已实现 | |
| `write_file` | ✅ 已实现 | 未注册到 registry |
| `edit_file` | ✅ 已实现 | |
| `glob` | ✅ 已实现 | |
| `grep` | ✅ 已实现 | |
| `ask_user` | ✅ 已实现 | |
| `skill` | ✅ 已实现 | |
| `todo_write` | ✅ 已实现 | |
| `agent` | ✅ 已实现 | subprocess worker |
| `task_create/get/list/output/stop` | ✅ 已实现 | |
| `send_message` | ✅ 已实现 | mailbox 投递 |
| `team_create/delete` | ✅ 已实现 | team CRUD |
| `brief` | 📋 暂不实现 | 精简输出模式 |
| `config` | 📋 暂不实现 | 运行时配置工具 |
| `sleep` | 📋 暂不实现 | 延迟工具 |
| `web_search` | 📋 暂不实现 | DuckDuckGo/SearXNG |
| `web_fetch` | 📋 暂不实现 | URL 内容抓取 |
| `tool_search` | 📋 暂不实现 | 工具元数据搜索 |
| `notebook_edit` | ❌ 不需要 | Python/Jupyter 场景 |
| `lsp` | 📋 暂不实现 | Language Server Protocol |
| `image_generation` | 📋 暂不实现 | DALL-E 等 |
| `image_to_text` | 📋 暂不实现 | 图像识别 |
| `cron_create/list/delete/toggle` | 📋 暂不实现 | 定时任务 |
| `remote_trigger` | 📋 暂不实现 | 远程触发器 |
| `mcp_auth` | 📋 暂不实现 | MCP 认证 |
| `list_mcp_resources/read_mcp_resource` | 📋 暂不实现 | MCP 资源 |
| `enter/exit_worktree` | ❌ 不需要 | 根据用户反馈暂不需要 |
| `enter/exit_plan_mode` | 📋 暂不实现 | Plan mode 工具 |
| `task_update` | 📋 暂不实现 | 任务状态更新 |

## Provider 对比

| OpenHarness Provider | CodeHarness 状态 |
| --- | --- |
| Anthropic API | ✅ 已实现 |
| OpenAI API | ✅ 已实现 |
| Echo (mock) | ✅ 已实现 |
| GitHub Copilot | 📋 暂不实现 |
| Codex Subscription | 📋 暂不实现 |
| Moonshot/Kimi | 📋 暂不实现 |

## 其他模块对比

| 模块 | OpenHarness | CodeHarness | 说明 |
| --- | --- | --- | --- |
| `auth` | 完整认证管理 | 环境变量 + credentials.json | Copilot OAuth、device flow 等未实现 |
| `channels` | Telegram/Slack/Discord 等 | 无 | ohmo 网关功能 |
| `autopilot` | 自动运行服务 | 无 | 定时/事件触发 agent 运行 |
| `bridge` | 外部桥接 | 无 | 桥接不同服务 |
| `personalization` | 个性化规则 | 无 | 用户行为学习 |
| `keybindings` | 自定义快捷键 | 无 | vim/emacs 模式 |
| `themes` | UI 主题 | 硬编码 | TUI 主题系统 |
| `voice` | 语音交互 | 无 | 语音输入/输出 |
| `vim` | Vim 模式 | 无 | |
| `output_styles` | 输出格式选择 | 无 | text/json/stream-json |
| `sandbox` | 沙箱执行 | 无 | macOS seatbelt、Docker |
| `hooks/hot_reload` | 热重载 | 无 | 文件监控 reload |

## OpenHarness 新增功能（CodeHarness 未移植）

### 1. Dry Run 模式
- `oh --dry-run` 预览配置 readiness（ready/warning/blocked）
- 不调用模型、不执行工具、不连接 MCP
- 解析 settings、auth、skills、commands、tools、MCP 配置

### 2. Auto-Compaction
- 上下文长度达到阈值时自动压缩历史
- 保留 task state 和 channel logs
- 支持多日会话

### 3. Cost Tracking
- Token 统计和费用计算
- 已预留 `CostTracker` 但未集成到 engine

### 4. Concurrent Tool Execution
- 并行工具调用（模型返回多个 tool_use）
- 当前 CodeHarness 顺序执行

### 5. ohmo Personal Agent
- `~/.ohmo` workspace
- `soul.md`、`identity.md`、`user.md`
- 网关：Telegram/Slack/Discord/Feishu
- 个人记忆

### 6. Provider Workflows
- 更完善的 profile 管理
- `oh setup` 交互式向导
- `oh provider list/use/add` CLI
