# Kimi Code 设计模式分析

## 1. Event Sourcing (事件溯源)

### 模式描述

所有状态变更记录为追加事件，存储在 `wire.jsonl` 文件中。

### 实现方式

```typescript
// 事件类型
type AgentEvent =
  | TurnStartedEvent | TurnEndedEvent
  | TurnStepStartedEvent | TurnStepCompletedEvent
  | AssistantDeltaEvent | ThinkingDeltaEvent
  | ToolCallStartedEvent | ToolCallDeltaEvent | ToolResultEvent
  | ToolProgressEvent
  | SubagentSpawnedEvent | SubagentCompletedEvent | SubagentFailedEvent
  | CompactionStartedEvent | CompactionCompletedEvent
  | BackgroundTaskStartedEvent | BackgroundTaskTerminatedEvent
  | AgentStatusUpdatedEvent
  | SkillActivatedEvent
  | McpServerStatusEvent;

// 事件分为两类
type LoopRecordedEvent = 
  | StepBeginEvent | StepEndEvent
  | ContentPartEvent
  | ToolCallEvent | ToolResultEvent;

type LoopLiveOnlyEvent = 
  | TextDeltaEvent | ThinkingDeltaEvent
  | ToolCallDeltaEvent | ToolProgressEvent;
```

### 优势

1. **崩溃恢复**：可以从任意事件点重放会话
2. **调试能力**：通过检查事件序列进行调试
3. **会话导出**：支持会话分享和迁移
4. **审计追踪**：完整的操作历史记录

### 在 CodeHarness 中的应用

当前项目使用 JSON snapshot 进行会话持久化，可以考虑：
- 将 `ToolResultBlock` 等操作记录为事件
- 实现事件重放机制
- 支持会话恢复和导出

## 2. Two-Phase Tool Execution (两阶段工具执行)

### 模式描述

工具执行分为两个阶段：
1. **resolveExecution()**：纯验证，无副作用
2. **execute()**：实际执行，有副作用

### 实现流程

```
Model returns tool_call
    │
    ▼
┌─────────────────────────────────────────────────────┐
│ Phase 1: resolveExecution()                        │
│  - 验证参数                                        │
│  - 检查权限                                        │
│  - 返回执行计划                                    │
└──────────────────────────┬──────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────┐
│ Phase 2: execute()                                 │
│  - 执行实际操作                                    │
│  - 产生副作用                                      │
│  - 返回结果                                        │
└─────────────────────────────────────────────────────┘
```

### 优势

1. **权限检查**：在两个阶段之间进行权限检查
2. **UI 预览**：UI 可以在执行前显示将要发生什么
3. **取消支持**：支持在 I/O 之前取消
4. **测试友好**：可以单独测试验证逻辑

### 在 CodeHarness 中的应用

当前项目的工具执行是单阶段的，可以考虑：
- 将工具执行分为 validate 和 execute 两个阶段
- 在 validate 阶段进行权限检查
- 在 UI 中显示执行计划

## 3. Stateless Loop (无状态循环)

### 模式描述

`runTurn()` 函数无隐藏状态，所有依赖通过参数注入。

### 实现方式

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

### 优势

1. **可测试性**：可以轻松 mock 所有依赖
2. **可移植性**：可以在不同环境中运行
3. **依赖清晰**：所有依赖显式声明
4. **并发安全**：无共享状态，可以并行运行

### 在 CodeHarness 中的应用

当前项目的 `run_query` 函数已经有一定的无状态特性，可以进一步：
- 将所有依赖显式传递
- 移除隐藏的全局状态
- 增强可测试性

## 4. Progressive Disclosure (渐进式披露)

### 模式描述

文档从高层到详细组织，智能体从稳定切入点开始，被指导下一步该去哪里查看。

### 实现方式

```
AGENTS.md (目录)
    │
    ├── ARCHITECTURE.md (高层架构)
    │
    ├── design-docs/ (设计原则)
    │   ├── core-beliefs.md
    │   ├── system-overview.md
    │   └── data-flow.md
    │
    ├── exec-plans/ (执行计划)
    │   ├── active/
    │   └── completed/
    │
    └── references/ (模块参考)
        ├── kaos-interface.md
        ├── kosong-interface.md
        └── ...
```

### 优势

1. **上下文管理**：避免信息过载
2. **导航清晰**：智能体知道下一步该看哪里
3. **维护性**：文档结构清晰，易于维护
4. **可扩展性**：可以轻松添加新文档

### 在 CodeHarness 中的应用

当前项目的文档结构已经有一定的渐进式披露，可以进一步：
- 将 AGENTS.md 精简为目录
- 建立清晰的文档层次
- 为智能体提供导航指引

## 5. Kaos Abstraction (执行环境抽象)

### 模式描述

所有文件系统和进程操作通过统一的 `Kaos` 接口。

### 实现方式

```typescript
interface Kaos {
  // 文件系统操作
  readText(path: string): Promise<string>;
  writeText(path: string, content: string): Promise<void>;
  glob(pattern: string): Promise<string[]>;
  stat(path: string): Promise<FileInfo>;
  iterdir(path: string): AsyncIterable<FileInfo>;
  
  // 进程执行
  exec(command: string, options?: ExecOptions): Promise<ExecResult>;
  
  // 路径操作
  resolve(...segments: string[]): string;
  relative(from: string, to: string): string;
}

// 实现
class LocalKaos implements Kaos { ... }
class SSHKaos implements Kaos { ... }
```

### 优势

1. **可测试性**：可以 mock Kaos 进行测试
2. **可移植性**：支持本地和远程执行
3. **统一接口**：所有工具使用相同的 API
4. **安全隔离**：可以限制文件系统访问

### 在 CodeHarness 中的应用

当前项目没有统一的执行环境抽象，可以考虑：
- 定义统一的 Kaos 接口
- 实现 LocalKaos 和 SSHKaos
- 将所有工具的 I/O 操作通过 Kaos 执行

## 6. Permission System (权限系统)

### 模式描述

三级权限模式，支持规则驱动的访问控制。

### 实现方式

```typescript
type PermissionMode = 'manual' | 'yolo' | 'auto';

interface PermissionRule {
  pattern: string;  // 工具名模式
  action: 'allow' | 'deny' | 'ask';
}

class PermissionManager {
  // 权限检查
  async beforeToolCall(toolCall: ToolCall): Promise<PermissionResult> {
    // 1. 规则匹配
    // 2. 内置工具默认权限
    // 3. Policy 评估
    // 4. 敏感文件检测
    // 5. 用户审批请求
  }
}
```

### 优势

1. **灵活性**：支持多种权限模式
2. **安全性**：敏感操作需要用户确认
3. **可配置**：规则可以通过配置文件定义
4. **可扩展**：支持自定义 Policy

### 在 CodeHarness 中的应用

当前项目有 `PermissionChecker`，可以进一步：
- 实现三级权限模式
- 支持规则 DSL
- 增加 Policy 评估系统

## 7. Hook System (钩子系统)

### 模式描述

支持 13 种钩子事件，通过正则匹配器匹配工具名/事件。

### 实现方式

```typescript
type HookEvent = 
  | 'PreToolUse' | 'PostToolUse' | 'PostToolUseFailure'
  | 'UserPromptSubmit' | 'Stop' | 'StopFailure'
  | 'SessionStart' | 'SessionEnd'
  | 'SubagentStart' | 'SubagentStop'
  | 'PreCompact' | 'PostCompact' | 'Notification';

interface Hook {
  event: HookEvent;
  matcher: string;  // 正则表达式
  command: string;  // 外部命令
}

class HookEngine {
  async executeHooks(event: HookEvent, context: HookContext): Promise<HookResult> {
    // 1. 匹配钩子
    // 2. 执行外部命令
    // 3. 返回 allow/block 决策
  }
}
```

### 优势

1. **可扩展性**：支持自定义钩子
2. **灵活性**：正则匹配器支持复杂模式
3. **安全性**：钩子失败不阻塞 Agent (Fail Open)
4. **可观测性**：钩子执行可以记录日志

### 在 CodeHarness 中的应用

当前项目有 Hooks 系统，可以进一步：
- 实现 13 种钩子事件
- 支持正则匹配器
- 增加 Fail Open 策略

## 8. Context Compaction (上下文压缩)

### 模式描述

使用 LLM 生成摘要，替换旧的对话历史。

### 实现方式

```typescript
class FullCompaction {
  async compact(messages: Message[]): Promise<Message[]> {
    // 1. 选择需要压缩的消息
    // 2. 使用 LLM 生成摘要
    // 3. 替换旧消息为摘要
    // 4. 保留最近的消息
  }
}
```

### 优势

1. **上下文管理**：避免上下文窗口溢出
2. **信息保留**：保留重要信息
3. **可配置**：支持自定义压缩策略
4. **自动化**：自动触发压缩

### 在 CodeHarness 中的应用

当前项目有 Compaction 计划，可以：
- 实现 LLM 驱动的摘要生成
- 支持手动和自动压缩
- 保留最近的对话历史

## 9. ToolScheduler (工具调度器)

### 模式描述

基于资源访问冲突的工具并发调度。

### 实现方式

```typescript
type ToolAccess = 'read' | 'readwrite' | 'write' | 'search';

class ToolScheduler {
  // 分析工具访问冲突
  analyzeConflicts(toolCalls: ToolCall[]): ConflictGroup[] {
    // 1. 识别文件访问类型
    // 2. 检测读写冲突
    // 3. 分组并发执行
  }
  
  // 并发执行工具
  async executeToolCalls(toolCalls: ToolCall[]): Promise<ToolResult[]> {
    // 1. 分析冲突
    // 2. 并发执行无冲突的工具
    // 3. 串行执行有冲突的工具
  }
}
```

### 优势

1. **性能优化**：并发执行提高效率
2. **安全性**：避免读写冲突
3. **可预测性**：工具执行顺序可预测
4. **可扩展性**：支持自定义冲突规则

### 在 CodeHarness 中的应用

当前项目没有工具调度器，可以考虑：
- 实现 ToolAccesses 分析
- 支持并发和串行执行
- 增加冲突检测和调度

## 10. Background Tasks (后台任务)

### 模式描述

支持后台执行 bash 命令和子代理任务。

### 实现方式

```typescript
type TaskType = 'bash' | 'agent';

interface BackgroundTask {
  id: string;
  type: TaskType;
  status: TaskStatus;
  command?: string;
  agentId?: string;
  result?: TaskResult;
}

class BackgroundManager {
  // 创建后台任务
  createTask(type: TaskType, options: TaskOptions): BackgroundTask;
  
  // 监控任务状态
  monitorTask(taskId: string): AsyncIterable<TaskStatus>;
  
  // 终止任务
  terminateTask(taskId: string): Promise<void>;
}
```

### 优势

1. **非阻塞**：后台执行不阻塞主流程
2. **可观测性**：支持任务状态监控
3. **可终止**：支持终止后台任务
4. **可重用**：任务结果可以重用

### 在 CodeHarness 中的应用

当前项目有后台任务计划，可以：
- 实现 BackgroundManager
- 支持 bash 和 agent 任务类型
- 增加任务状态监控

## 11. RPC Isolation (RPC 隔离)

### 模式描述

Agent Core 和 UI 之间通过 RPC 接口通信。

### 实现方式

```
TUI/CLI  ←──SDKAgentRPC──→  Agent  ←──SDKSessionRPC──→  Session
         (events, approvals)         (tool calls, config)
```

### 优势

1. **解耦**：UI 和 Agent Core 解耦
2. **可测试性**：可以单独测试 UI 或 Agent
3. **可替换性**：可以替换 UI 或 Agent 实现
4. **安全性**：RPC 边界可以进行权限控制

### 在 CodeHarness 中的应用

当前项目使用事件驱动，可以进一步：
- 定义清晰的 RPC 接口
- 实现 SDK 层
- 增加 RPC 边界的安全控制

## 12. Profile System (配置文件系统)

### 模式描述

预定义的 Agent Profile，定义系统提示词、工具和子代理。

### 实现方式

```typescript
interface AgentProfile {
  name: string;
  systemPrompt: string;
  tools: string[];
  subagents: string[];
  whenToUse: string;
}

// 预定义 Profile
const profiles: Record<string, AgentProfile> = {
  agent: { ... },
  coder: { ... },
  explore: { ... },
  plan: { ... },
};
```

### 优势

1. **可配置**：支持自定义 Profile
2. **可重用**：Profile 可以在多个 Agent 间共享
3. **可组合**：Profile 可以嵌套子代理
4. **可维护**：Profile 集中管理

### 在 CodeHarness 中的应用

当前项目有类似的配置，可以进一步：
- 实现 Profile 系统
- 支持 YAML 配置文件
- 增加 Profile 继承机制
