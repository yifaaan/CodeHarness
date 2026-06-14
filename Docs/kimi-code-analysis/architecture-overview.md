# Kimi Code 系统架构概述

## 顶层目录结构

```
kimi-code/
├── .agents/                  # Agent 技能定义目录
├── .changeset/               # Changeset 版本管理
├── .github/                  # GitHub Actions / CI 配置
├── apps/
│   ├── kimi-code/            # 主 CLI/TUI 应用入口 (161 src files)
│   └── vis/                  # 可视化调试工具 (server + web)
├── build/                    # 构建输出
├── docs/                     # 项目文档
├── packages/
│   ├── agent-core/           # 统一代理引擎 (核心)
│   ├── kaos/                 # 执行环境和文件/进程抽象
│   ├── kosong/               # LLM/Provider 抽象层
│   ├── migration-legacy/     # 旧版迁移工具
│   ├── node-sdk/             # 公共 TypeScript SDK 和 Harness
│   ├── oauth/                # Kimi OAuth 和托管认证
│   └── telemetry/            # 客户端遥测基础设施
├── package.json              # Monorepo 根配置
├── pnpm-workspace.yaml       # pnpm 工作区
├── tsconfig.json             # TypeScript 全局配置
└── vitest.config.ts          # 测试配置
```

## 分层架构

```
┌────────────────────────────────────────────────────────────────────┐
│  CLI/TUI Layer (apps/kimi-code)                                   │
│  - Command-line parsing                                           │
│  - Terminal UI (pi-tui framework)                                 │
│  - Reverse-RPC for approvals/questions                            │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ RPC (in-process)
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  SDK Layer (packages/kimi-code-sdk)                               │
│  - KimiHarness (entry point)                                      │
│  - Session management                                             │
│  - Event subscription                                             │
└──────────────────────────┬─────────────────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────────────────┐
│  Agent Core (packages/agent-core)                                 │
│                                                                   │
│  ┌───────────┐    ┌───────────┐    ┌────────────────────────────┐ │
│  │  Session  │───>│   Agent   │───>│        TurnFlow            │ │
│  │ (manager) │    │(orchestr.)│    │  (prompt/steer/cancel)     │ │
│  └─────┬─────┘    └─────┬─────┘    └───────────┬────────────────┘ │
│        │                │                       │                  │
│        │                │                       ▼                  │
│        │                │            ┌────────────────────┐        │
│        │                │            │  runTurn (Loop)    │        │
│        │                │            │  LLM → Tools loop  │        │
│        │                │            └────────────────────┘        │
│        │                │                       │                  │
│        │                │          ┌───────────┼──────────┐       │
│        │                │          ▼           ▼          ▼      │
│        │                │    ┌────────┐ ┌──────────┐ ┌─────────┐ │
│        │                │    │ Tools  │ │ Perm.    │ │ Hooks   │ │
│        │                │    └───┬────┘ └──────────┘ └─────────┘ │
│        │                │        │                               │
│        │                │        ▼                               │
│        │                │  ┌────────────────┐                   │
│        │                │  │ kaos (I/O)     │──> filesystem     │
│        │                │  │ kosong (LLM)   │──> HTTP APIs       │
│        │                │  │ MCP Manager    │──> ext. servers    │
│        │                │  └────────────────┘                   │
│        │                │                                        │
│        │                │  ┌──────────────┐                     │
│        │                │  │ AgentRecords │──> wire.jsonl        │
│        │                │  └──────────────┘                     │
└────────┼────────────────┼────────────────────────────────────────┘
         │                │
         ▼                ▼
┌────────────────────────────────────────────────────────────────────┐
│  Infrastructure Layer                                             │
│  - Kaos: Filesystem/process abstraction                           │
│  - Kosong: LLM provider abstraction (Anthropic, OpenAI, etc.)    │
│  - MCP: Model Context Protocol client                             │
└────────────────────────────────────────────────────────────────────┘
```

## 核心数据流

### 用户提示流程

```
User Input
    │
    ▼
┌─────────────┐
│ TurnFlow    │
│ .prompt()   │
└──────┬──────┘
       │
       ▼
┌────────────────────────────────────────────────────┐
│ runTurn() — Stateless Loop                         │
│                                                    │
│  repeat:                                           │
│    beforeStep() ──> executeLoopStep()            │
│                          │                         │
│               ┌──────────┴──────────┐             │
│               ▼                     ▼             │
│         LLM.chat()             tool execution     │
│               │                     │             │
│               ▼                     ▼             │
│         streamed response      ToolResults        │
│               │                     │             │
│               ▼                     ▼             │
│         stopReason:            stopReason:        │
│         end_turn ───> EXIT    tool_use ───> loop │
│                                                    │
└────────────────────────────────────────────────────┘
       │
       ▼
TurnEndResult → Events → TUI
```

### 工具执行流程

```
Model returns tool_call
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ Tool Execution Pipeline                                     │
│                                                             │
│  1. Preflight (validate args)                              │
│     │                                                       │
│     ▼                                                       │
│  2. PrepareToolExecution hook (PreToolUse 钩子)            │
│     │                                                       │
│     ▼                                                       │
│  3. PermissionManager.beforeToolCall (权限检查)            │
│     │                                                       │
│     ▼                                                       │
│  4. Policy 评估 (ask-user, plan, yolo, git-cwd-write)     │
│     │                                                       │
│     ▼                                                       │
│  5. resolveExecution (解析工具执行)                        │
│     │                                                       │
│     ▼                                                       │
│  6. execute (实际执行)                                     │
│     │                                                       │
│     ▼                                                       │
│  7. FinalizeToolResult hook (PostToolUse 钩子)             │
│     │                                                       │
│     ▼                                                       │
│  8. dispatchEvent (tool.result)                            │
└─────────────────────────────────────────────────────────────┘
```

## 包依赖图

```
                    ┌─────────────────────┐
                    │   apps/kimi-code    │
                    │   (CLI + TUI)       │
                    └──────────┬──────────┘
                               │ depends on
                               ▼
                    ┌─────────────────────┐
                    │  kimi-code-sdk      │
                    └──────────┬──────────┘
                               │ depends on
                               ▼
    ┌─────────────────────────────────────────────────┐
    │              agent-core                         │
    │  (agent, session, loop, tools, MCP, RPC,        │
    │   permissions, hooks, skills, records)         │
    └────┬──────────┬──────────┬──────────┬───────────┘
         │          │          │          │
         ▼          ▼          ▼          ▼
    ┌───────┐ ┌─────────┐ ┌─────────┐ ┌──────────────┐
    │ kaos  │ │ kosong  │ │ oauth   │ │ telemetry    │
    │ (I/O) │ │ (LLM)   │ │ (auth)  │ │ (observ.)    │
    └───────┘ └─────────┘ └─────────┘ └──────────────┘
```

## 关键设计决策

### 1. Event Sourcing

所有状态变更记录为追加事件，存储在 `wire.jsonl` 中：

```
Agent action
    │
    ├── logRecord({ type: 'turn.prompt', ... })
    ├── Execute action
    ├── logRecord({ type: 'context.append_message', ... })
    │
    └── All records → wire.jsonl
```

**优势**：
- 会话重放和崩溃恢复
- 通过检查事件序列进行调试
- 会话导出和分享

### 2. Two-Phase Tool Execution

工具实现两个阶段：
- `resolveExecution()`：纯验证，无副作用
- `execute()`：实际执行，有副作用

**优势**：
- 权限检查发生在两个阶段之间
- UI 可以在执行前显示将要发生什么
- 支持在 I/O 之前取消

### 3. Stateless Loop

`runTurn()` 函数无隐藏状态：

```typescript
async function runTurn(input: {
  turnId: string;
  signal: AbortSignal;
  llm: LLM;
  buildMessages: () => Message[];
  dispatchEvent: LoopEventDispatcher;
  tools?: ExecutableTool[];
  hooks?: LoopHooks;
  maxSteps?: number;
}): Promise<TurnResult>
```

**优势**：
- 可测试性
- 可移植性
- 依赖注入清晰

### 4. Kaos Abstraction

所有文件系统和进程操作通过 `Kaos` 接口：

- `LocalKaos`：本地机器
- `SSHKaos`：远程机器

**优势**：
- 工具可测试
- 工具可移植
- 统一的执行环境

## 事件架构

```
Agent ──emit──> AgentEvent ──> RPC ──> SDK ──> TUI.render()
  │                                                │
  │  Events: turn.started, turn.ended,            │
  │  assistant.delta, tool.call.started,           │
  │  tool.result, thinking.delta,                  │
  │  error, agent.status.updated,                  │
  │  subagent.spawned/completed/failed             │
  │                                                │
  └──record──> AgentRecords ──> wire.jsonl        │
                                                   │
  (Approval/Question RPC)                          │
  TUI ──requestApproval──> Agent                   │
  TUI ──askQuestion──────> Agent
```

## 会话持久化布局

```
~/.kimi-code/
├── config.toml                        # 用户配置
├── mcp.json                           # MCP 服务器配置
├── session_index.jsonl                # 会话索引
├── sessions/
│   └── <workdir-key>/
│       └── <session-uuid>/
│           ├── state.json             # 会话元数据
│           └── agents/
│               ├── main/
│               │   └── wire.jsonl     # 事件记录
│               └── <subagent-id>/
│                   └── wire.jsonl
└── credentials/
    └── mcp/                           # MCP OAuth 令牌
```

## 技术栈

| 类别 | 技术 |
|------|------|
| **运行时** | Node.js ≥ 24.15.0 |
| **语言** | TypeScript 6.0.2 (strict mode, ESM) |
| **包管理** | pnpm 10+ (workspace monorepo) |
| **TUI 框架** | `@earendil-works/pi-tui` |
| **LLM SDKs** | Anthropic, OpenAI, Google GenAI, Moonshot |
| **测试** | vitest |
| **Lint** | oxlint (type-aware) |
| **构建** | tsdown (TypeScript bundler) |

## 重构价值点

### 高价值模块（建议优先重构）

1. **ToolScheduler**：基于资源访问冲突的工具并发调度
2. **PermissionManager**：三级权限模式（manual/yolo/auto）
3. **HookEngine**：13 种钩子事件，支持外部命令执行
4. **Context Compaction**：LLM 驱动的上下文压缩
5. **Background Tasks**：强大的后台任务系统

### 可复用设计模式

1. **渐进式披露**：文档组织方式
2. **事件溯源**：状态管理方式
3. **两阶段执行**：工具安全机制
4. **无状态循环**：核心执行引擎设计
