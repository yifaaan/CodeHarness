# Kimi Code 核心组件详解

## 1. Agent 类 (agent-core/src/agent/index.ts)

Agent 类是整个系统的核心协调器，组合了所有子系统：

```typescript
class Agent {
  // 核心子系统
  readonly config: ConfigState;          // 模型配置、system prompt
  readonly context: ContextMemory;       // 对话历史
  readonly turn: TurnFlow;               // Turn 生命周期管理
  readonly tools: ToolManager;           // 工具注册和管理
  readonly permission: PermissionManager;// 权限系统
  readonly injection: InjectionManager;  // 动态注入 (plan mode, permission mode)
  readonly planMode: PlanMode;           // 计划模式
  readonly usage: UsageRecorder;         // Token 用量追踪
  readonly fullCompaction: FullCompaction;// 上下文压缩
  readonly records: AgentRecords;        // 持久化记录
  readonly background: BackgroundManager;// 后台任务
  readonly skills?: SkillManager;        // 技能管理
  readonly hooks?: HookEngine;           // 生命周期钩子
  readonly mcp?: McpConnectionManager;   // MCP 连接
  readonly replayBuilder: ReplayBuilder; // 回放构建器
  
  // RPC 方法暴露给 SDK
  get rpcMethods(): AgentAPI { ... }
}
```

### 构造顺序

1. **ConfigState**：加载配置，解析 model、system prompt
2. **ContextMemory**：初始化对话历史，设置 token 计数
3. **ToolManager**：注册内置工具 + MCP 工具 + 用户工具
4. **PermissionManager**：加载权限规则，设置策略
5. **HookEngine**：加载钩子配置，注册事件处理器
6. **TurnFlow**：创建 turn 管理器，绑定 agent 引用
7. **BackgroundManager**：初始化后台任务系统
8. **AgentRecords**：设置事件记录和 wire.jsonl

## 2. Loop 系统 (agent-core/src/loop/)

无状态代理循环，是核心执行引擎：

### runTurn() 函数

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

### 执行流程

```
runTurn(input)
  → while (true):
      → signal.throwIfAborted()
      → check maxSteps
      → executeLoopStep()
          → beforeStep hook (注入计划模式、权限模式)
          → buildMessages() (从 ContextMemory 构建)
          → llm.chat() (带重试)
          → dispatchEvent (step.begin, content.part, step.end)
          → if tool_use: runToolCallBatch()
              → preflightToolCall (验证)
              → prepareToolCall (钩子 + 权限)
              → ToolScheduler 并发执行
              → finalizePendingToolResult
              → dispatchEvent (tool.call, tool.result)
          → if end_turn: break
      → shouldContinueAfterStop hook (steer buffer, stop hooks)
```

### 关键设计

- **ToolScheduler**：根据资源访问冲突决定工具并发/串行执行
- **ToolAccesses**：文件读写冲突检测 (read/readwrite/write/search)
- **chatWithRetry**：指数退避重试，支持可重试错误检测

## 3. TurnFlow (agent-core/src/agent/turn/index.ts)

管理 Turn 的完整生命周期：

### 主要方法

- `prompt()`：新用户输入 -> 启动新 turn
- `steer()`：中途输入 (缓冲后在下一个 beforeStep 注入)
- `cancel()`：中止当前 turn

### turnWorker() 执行流程

1. **UserPromptSubmit 钩子**：用户提交前的钩子
2. **runTurn (循环)**：执行代理循环
3. **上下文溢出处理**：compaction + retry
4. **错误分类和遥测**：记录错误类型和遥测数据

## 4. ToolManager (agent-core/src/agent/tool/index.ts)

管理三类工具：

### 工具类型

1. **Builtin Tools**：
   - Read, Write, Edit, Grep, Glob, Bash
   - ReadMediaFile, EnterPlanMode, ExitPlanMode
   - AskUserQuestion, TodoList, TaskList, TaskOutput, TaskStop
   - Skill, Agent, WebSearch, FetchURL

2. **User Tools**：通过 `registerTool` RPC 注册的自定义工具

3. **MCP Tools**：通过 MCP 协议连接的外部工具服务器

### 工具激活

由 Profile 的 `tools` 字段控制，支持 glob 模式：
```json
{
  "tools": ["read", "write", "mcp__github__*"]
}
```

## 5. PermissionManager (agent-core/src/agent/permission/index.ts)

三级权限模式：

### 权限模式

| 模式 | 描述 |
|------|------|
| **manual** | 规则驱动；未匹配的工具调用需要用户确认 |
| **yolo** | 只有 deny 规则能阻止；其他全部允许 |
| **auto** | AFK 模式，可绕过规则检查 |

### 权限检查流程

1. **规则匹配**：allow/deny/ask
2. **内置工具默认权限**：
   - Read/Grep/Glob = auto_allow
   - Bash/Write/Edit = ask
3. **Policy 评估**：ask-user-question, plan-mode, yolo-workspace-access, git-cwd-write
4. **敏感文件检测**：`.env`, `id_rsa`, `credentials` 等
5. **用户审批请求**：通过 RPC 发送到 TUI

## 6. ContextMemory (agent-core/src/agent/context/index.ts)

管理对话历史和 token 计数：

### 核心功能

- **延迟消息**：工具交换期间 (有 pending tool result) 的消息被延迟
- **压缩**：支持 compaction 替换旧消息为摘要
- **投影**：`project()` 将内部 ContextMessage 转换为 LLM Message
- **Open step 追踪**：通过 `openSteps` Map 追踪未完成的 step

## 7. HookEngine (agent-core/src/agent/hooks/engine.ts)

支持 13 种钩子事件：

### 钩子事件类型

```
PreToolUse, PostToolUse, PostToolUseFailure,
UserPromptSubmit, Stop, StopFailure,
SessionStart, SessionEnd,
SubagentStart, SubagentStop,
PreCompact, PostCompact, Notification
```

### 执行机制

- 通过正则匹配器匹配工具名/事件
- 执行外部命令
- 返回 allow/block 决策

## 8. Session 管理 (agent-core/src/session/)

### SessionStore

基于文件系统的会话持久化：
```
~/.kimi/sessions/<workdir-key>/<session-id>/
├── state.json             # 会话元数据
├── state.json.lock        # 锁文件
└── agents/
    ├── main/
    │   └── wire.jsonl     # 事件记录
    └── <subagent-id>/
        └── wire.jsonl
```

### Session 类

管理多个 Agent、MCP 连接、Skills、Hooks：
- 支持 fork 操作 (复制会话)
- 支持会话恢复
- 支持会话导出

## 9. Profile 系统 (agent-core/src/profile/)

预定义的 Agent Profile：

| Profile | 描述 | 工具 |
|---------|------|------|
| **agent** | 主代理 profile | 所有工具 + 子代理 |
| **coder** | 代码编写子代理 | 代码相关工具 |
| **explore** | 代码探索子代理 | 只读工具 |
| **plan** | 计划代理 | 计划相关工具 |

每个 Profile 定义：
```json
{
  "systemPrompt": "模板化的系统提示词",
  "tools": ["read", "write", "edit"],
  "subagents": ["coder", "explore"],
  "whenToUse": "使用场景描述"
}
```

## 10. MCP 集成 (agent-core/src/mcp/)

### McpConnectionManager

管理 MCP 服务器连接：

- **stdio 传输**：本地进程通信
- **HTTP 传输**：远程服务器通信
- **OAuth 认证**：支持 OAuth 令牌

### 连接生命周期

1. **发现**：从配置文件加载服务器
2. **连接**：建立传输连接
3. **初始化**：交换 capabilities
4. **工具发现**：获取可用工具列表
5. **工具调用**：通过 MCP 协议调用工具
6. **断开**：清理连接资源

## 11. Background Tasks (agent-core/src/agent/background/)

### BackgroundManager

管理后台任务：

- **Bash Tasks**：后台执行 shell 命令
- **Agent Tasks**：后台执行子代理任务

### 任务状态

```typescript
type TaskStatus = 
  | 'pending'      // 等待执行
  | 'running'      // 正在执行
  | 'completed'    // 执行完成
  | 'failed'       // 执行失败
  | 'terminated'   // 被终止
```

## 12. TUI (apps/kimi-code/src/tui/kimi-tui.ts)

基于 `pi-tui` 框架构建的终端 UI：

### 布局结构

```
transcript -> activity -> todoPanel -> queue -> editor -> footer
```

### 主要组件

- **AssistantMessage**：助手消息显示
- **ToolCall**：工具调用显示
- **Thinking**：思考过程显示
- **UserMessage**：用户消息显示
- **SkillActivation**：技能激活显示
- **ApprovalPanel**：审批面板
- **QuestionDialog**：问题对话框
- **TasksBrowser**：任务浏览器

### 交互功能

斜杠命令：
- `/help`, `/new`, `/model`, `/permission`
- `/compact`, `/init`, `/fork`, `/login`
- `/status`, `/version`, `/settings`

### 媒体支持

- 图片粘贴和显示
- 视频粘贴和显示
- 文件拖拽支持

## 13. SDK 层 (packages/node-sdk/)

### KimiHarness

应用层 Harness，管理会话、配置、认证：

```typescript
class KimiHarness {
  // 核心功能
  createSession(config: SessionConfig): Session;
  listSessions(): SessionInfo[];
  resumeSession(sessionId: string): Session;
  
  // 配置管理
  getConfig(): KimiConfig;
  updateConfig(config: Partial<KimiConfig>): void;
  
  // 认证
  login(): Promise<void>;
  logout(): void;
}
```

### SDKRpcClient

RPC 客户端，用于 TUI 与 Agent Core 通信：

- **Agent 级别 RPC**：prompt, steer, cancel, setModel, approve
- **Session 级别 RPC**：createSession, listSessions, getConfig

## 14. Provider 系统 (packages/kosong/)

### ChatProvider 接口

统一的 LLM 调用接口：

```typescript
interface ChatProvider {
  generate(messages: Message[], options?: GenerateOptions): Promise<Response>;
  withThinking(messages: Message[], options?: GenerateOptions): AsyncIterable<ThinkingDelta>;
  uploadVideo(video: VideoInput): Promise<VideoReference>;
}
```

### 内置 Provider

- **kimi.ts**：Moonshot AI Kimi
- **anthropic.ts**：Anthropic Claude
- **openai-responses.ts**：OpenAI Responses API
- **openai-legacy.ts**：OpenAI Legacy API
- **google-genai.ts**：Google GenAI

### 统一消息模型

```typescript
type Message = {
  role: 'user' | 'assistant' | 'system';
  content: ContentPart[];
};

type ContentPart = 
  | { type: 'text'; text: string }
  | { type: 'tool_use'; toolCall: ToolCall }
  | { type: 'tool_result'; toolResult: ToolResult }
  | { type: 'image'; image: ImageInput }
  | { type: 'video'; video: VideoInput };
```
