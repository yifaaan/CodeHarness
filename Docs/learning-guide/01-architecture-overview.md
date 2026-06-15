# 第1章：系统架构总览

> 理解 CodeHarness 的整体分层设计和核心设计原则。

## 1. 整体架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              CLI / TUI 层                                    │
│                        (命令行入口、用户交互)                                 │
└─────────────────────────────────────┬───────────────────────────────────────┘
                                      │ 调用
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Agent 层                                        │
│                     (组合根、协调各子系统)                                    │
│  ┌─────────────────────────────────────────────────────────────────────┐    │
│  │  Agent                                                              │    │
│  │  - Prompt()    发起对话                                              │    │
│  │  - Cancel()    取消执行                                              │    │
│  │  - SetPermissionMode()  设置权限模式                                 │    │
│  │  - SetHookEngine()      设置钩子引擎                                 │    │
│  └─────────────────────────────────────────────────────────────────────┘    │
└───────┬───────────┬───────────┬───────────┬───────────┬───────────┬─────────┘
        │           │           │           │           │           │
        ▼           ▼           ▼           ▼           ▼           ▼
┌───────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐
│Engine 层  │ │Session  │ │Permission│ │ Records │ │ Context │ │ Hooks   │
│           │ │  层     │ │   层     │ │   层    │ │   层    │ │   层    │
│ Loop      │ │ Session │ │Permission│ │AgentRecs│ │ContextMem│ │HookEngine│
│ToolMgr    │ │Store    │ │  Gate    │ │         │ │Compactor│ │         │
└─────┬─────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘ └─────────┘
      │
      ├─────────────────────────────────────────────────────────┐
      │                                                         │
      ▼                                                         ▼
┌───────────────────┐                                     ┌───────────────┐
│    Tools 层       │                                     │    LLM 层     │
│                   │                                     │               │
│  BashTool         │                                     │ ChatProvider  │
│  ReadFile         │                                     │ OpenAiProvider│
│  WriteFile        │                                     │ ...           │
│  Glob             │                                     └───────┬───────┘
│  Grep             │                                             │
│  ...              │                                             │ HTTP
└─────────┬─────────┘                                             ▼
          │                                               ┌───────────────┐
          │                                               │ 外部 LLM API  │
          │                                               │ (OpenAI, etc) │
          │                                               └───────────────┘
          │
          ▼
┌───────────────────┐
│    Host 层        │  ← 最底层，所有 I/O 操作都通过这里
│                   │
│  Host (interface) │
│  LocalHost        │
│  HostProcess      │
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  操作系统 API     │
│  (文件、进程)      │
└───────────────────┘
```

## 2. 分层职责

### 2.1 Host 层（最底层）

**职责**：抽象文件系统和进程操作

**为什么需要**：
- 测试时可以 Mock，不依赖真实文件系统
- 未来可以扩展到远程执行（SSH）
- 统一不同操作系统的差异

**关键类**：
- `Host`：接口定义
- `LocalHost`：本地实现
- `HostProcess`：进程抽象

**详见**：[02-host-layer.md](02-host-layer.md)

### 2.2 LLM 层

**职责**：统一多个 LLM Provider 的接口

**为什么需要**：
- OpenAI、Anthropic、Google 等 API 各不相同
- 需要统一格式，让上层代码不用关心具体 Provider
- 支持流式响应、工具调用等通用功能

**关键类**：
- `ChatProvider`：统一接口
- `OpenAiProvider`：OpenAI 适配器
- `StreamCallbacks`：流式回调

**详见**：[03-llm-layer.md](03-llm-layer.md)

### 2.3 Tools 层

**职责**：定义 Agent 能执行的所有操作

**为什么需要**：
- Agent 的"手脚"
- 每个工具实现特定功能（读文件、执行命令等）
- 通过两阶段执行保证安全

**关键类**：
- `ExecutableTool`：工具接口
- `BashTool`、`ReadFile`、`WriteFile` 等：具体工具

**详见**：[04-tool-system.md](04-tool-system.md)

### 2.4 Engine 层

**职责**：核心执行循环

**为什么需要**：
- 实现 "LLM → 工具 → LLM → ..." 的循环
- 无状态设计，易于测试
- 支持取消、重试等控制

**关键类**：
- `Loop`：执行循环
- `TurnInput` / `TurnResult`：输入输出结构
- `LoopHooks`：扩展钩子

**详见**：[05-loop-engine.md](05-loop-engine.md)

### 2.5 Agent 层

**职责**：组合根，协调所有子系统

**为什么需要**：
- 对外提供统一接口（Prompt、Cancel）
- 管理对话历史、权限、工具等
- 处理事件分发

**关键类**：
- `Agent`：核心类

**详见**：[06-agent-core.md](06-agent-core.md)

### 2.6 Permission 层

**职责**：控制危险操作的执行

**为什么需要**：
- 防止误删文件
- 防止执行危险命令
- 提供用户确认机制

**关键类**：
- `PermissionGate`：权限门控
- `PermissionMode`：权限模式（Yolo/Manual/Auto）

**详见**：[07-permission-system.md](07-permission-system.md)

### 2.7 Session / Records 层

**职责**：会话管理和事件溯源

**为什么需要**：
- 持久化对话，支持恢复
- 记录所有操作，用于调试
- 支持回放

**关键类**：
- `AgentRecords`：事件记录
- `Session`：会话管理
- `SessionStore`：存储管理

**详见**：[08-session-records.md](08-session-records.md)

### 2.8 Context 层

**职责**：管理上下文内存和 Token 估算，实现自动压缩

**为什么需要**：
- LLM 有 Context Window 限制
- 对话历史不能无限增长
- 需要智能压缩以保持连贯性

**关键类**：
- `ContextMemory`：消息存储 + Token 缓存
- `TokenEstimate`：启发式 Token 估算
- `Compactor`：上下文压缩器

**详见**：[10-context-memory.md](10-context-memory.md)

### 2.9 Hooks 层

**职责**：运行用户配置的生命周期钩子

**为什么需要**：
- 用户自定义扩展点
- 安全策略注入
- 审计日志

**关键类**：
- `HookEngine`：钩子执行引擎
- `HookDef`：钩子定义
- `HookEvent`：11 种生命周期事件

**详见**：[11-hooks-system.md](11-hooks-system.md)

## 3. 八大设计原则

### 3.1 Event Sourcing（事件溯源）

**原则**：所有状态变化记录为追加事件

**实现**：
```
用户输入 → 记录 turn.prompt 事件
工具调用 → 记录 tool.call 事件
工具结果 → 记录 tool.result 事件
LLM响应 → 记录 assistant.message 事件
```

**好处**：
- 完整的操作历史，便于调试
- 支持回放恢复
- 无需复杂数据库

**代码体现**：
```cpp
// Agent.h:79
void SetRecords(records::AgentRecords* records);

// 每次操作都会记录
records->Log(RecordType::TurnPrompt, {...});
```

### 3.2 Two-Phase Tool Execution（两阶段工具执行）

**原则**：工具执行分为纯验证阶段和副作用阶段

**实现**：
```cpp
// Engine/Tool.h:47-48
class ExecutableTool {
    // 阶段1：纯验证，无副作用
    virtual absl::StatusOr<ToolExecution> ResolveExecution(const nlohmann::json& args) = 0;
    
    // 阶段2：执行，有副作用
    virtual absl::StatusOr<ToolResult> Execute(const nlohmann::json& args, const ToolContext& ctx) = 0;
};
```

**流程**：
```
工具调用请求
    ↓
ResolveExecution() → 返回 ToolExecution
    │                   ├── description（将发生什么）
    │                   └── requiresPermission（是否需要权限）
    ↓
权限检查（如果 requiresPermission）
    ↓
Execute() → 实际执行
```

**好处**：
- 权限检查在执行前
- UI 可以预览操作
- 可以取消

### 3.3 Stateless Loop（无状态循环）

**原则**：Loop 函数没有隐藏状态，所有依赖注入

**实现**：
```cpp
// Engine/Loop.h:9
TurnResult RunTurn(TurnInput input, const LoopHooks& hooks = {});

// TurnInput 包含所有依赖
struct TurnInput {
    llm::ChatProvider* provider;      // LLM Provider
    std::vector<ExecutableTool*> tools; // 工具列表
    host::Host* host;                 // Host
    std::string systemPrompt;         // 系统提示词
    std::vector<llm::Message> history;// 对话历史
    EventDispatcher dispatchEvent;    // 事件分发
    std::stop_token stopToken;        // 取消令牌
    int maxSteps = 1000;              // 最大步数
    permission::PermissionGate* permissionGate; // 权限门控
};
```

**好处**：
- 易于测试（mock 依赖即可）
- 易于理解（无隐藏状态）
- 易于移植

### 3.4 Progressive Disclosure（渐进式披露）

**原则**：文档从高层到详细，逐步展开

**实现**：
```
README.md（概览）
    ↓
01-architecture-overview.md（架构）
    ↓
02-host-layer.md（模块详解）
    ↓
源代码（实现细节）
```

**好处**：
- 快速了解全貌
- 按需深入
- 不会一开始就被细节淹没

### 3.5 Repository as Source of Truth（仓库即真理）

**原则**：所有知识都在代码仓库中

**实现**：
- 设计文档在 `docs/`
- 实现计划在 `docs/plan/`
- 代码即文档（清晰的命名、注释）

**好处**：
- 知识不散落在各处
- 版本控制
- 易于查找

### 3.6 Unified Abstractions（统一抽象）

**原则**：概念相似的操作使用统一接口

**实现**：
- `Host`：统一文件/进程操作（本地或远程）
- `ChatProvider`：统一 LLM 调用（不同 Provider）
- `ExecutableTool`：统一工具执行

**好处**：
- 易于扩展新实现
- 代码复用
- 降低认知负担

### 3.7 Fail Open for Hooks（钩子失败开放）

**原则**：Hook 失败不阻塞 Agent

**实现**：
```cpp
// 钩子执行失败时，默认允许继续
if (hooks.beforeStep) {
    auto result = hooks.beforeStep(step);
    if (!result || result->allow) {
        // 继续执行
    }
}
```

**好处**：
- 配置错误不阻塞
- 脚本失败不影响主流程
- 安全默认值

### 3.8 Context Compaction（上下文压缩）

**原则**：接近 token 限制时自动压缩旧消息

**实现**：
```
当 context 接近 75% 限制时：
    ↓
让 LLM 总结旧消息
    ↓
用摘要替换旧消息
    ↓
释放空间给新消息
```

**好处**：
- 避免超出 context window
- 保持对话连贯
- 自动管理

## 4. 依赖方向

**规则**：上层依赖下层，下层不知道上层

```
CLI/TUI → Agent → Engine → Tools → Host
                    ↓
                   LLM
```

**好处**：
- 单向依赖，易于理解
- 下层可独立测试
- 易于重构

## 5. 模块速查表

| 模块 | 核心类 | 职责 | 依赖 |
|------|--------|------|------|
| Host | Host, LocalHost | 文件/进程抽象 | 无（最底层） |
| LLM | ChatProvider, OpenAiProvider | LLM 统一接口 | 无 |
| Tools | ExecutableTool, BashTool | 可执行操作 | Host |
| Engine | Loop, TurnInput | 执行循环 | LLM, Tools, Host |
| Permission | PermissionGate | 权限控制 | 无 |
| Agent | Agent | 组合根 | Engine, Permission, Records, Context, Hooks |
| Session | Session, SessionStore | 会话管理 | Agent |
| Records | AgentRecords | 事件记录 | 无 |
| Context | ContextMemory, Compactor | 上下文内存与压缩 | LLM |
| Hooks | HookEngine, HookDef | 生命周期钩子 | Host |

## 6. 数据流向

### 6.1 用户请求流程

```
用户输入 "帮我分析项目"
        ↓
Agent.Prompt(text)
        ↓
构建 Message，加入 history
        ↓
Loop.RunTurn(input)
        ↓
ChatProvider.Generate() → 流式回调
        ↓
LLM 返回 ToolCall: Bash("ls -la")
        ↓
PermissionGate.ShouldRun() → 询问用户
        ↓
BashTool.Execute() via Host.Exec()
        ↓
工具结果 → Tool Message → history
        ↓
再次调用 LLM...
        ↓
LLM 返回最终回答
        ↓
返回给用户
```

### 6.2 事件流向

```
Loop 内部事件
    ↓
EventDispatcher
    ↓
┌───────────────────┬───────────────────┐
│                   │                   │
▼                   ▼                   ▼
AgentRecords    TUI 渲染          日志
(wire.jsonl)    (实时显示)        (spdlog)
```

## 7. 关键文件导航

```
Source/CodeHarness/
│
├── Agent/
│   ├── Agent.h          # Agent 类定义
│   ├── Agent.cpp        # Agent 实现
│   └── AgentTypes.h     # Agent 相关类型
│
├── Engine/
│   ├── Loop.h           # Loop 函数声明
│   ├── Loop.cpp         # Loop 实现（核心！）
│   ├── LoopTypes.h      # Loop 输入输出类型
│   ├── LoopHooks.h      # Loop 钩子接口
│   └── Tool.h           # ExecutableTool 接口
│
├── Host/
│   ├── Host.h           # Host 接口
│   ├── LocalHost.h      # 本地实现
│   └── HostProcess.h    # 进程抽象
│
├── Llm/
│   ├── ChatProvider.h   # Provider 接口
│   ├── OpenAiProvider.h # OpenAI 实现
│   ├── Types.h          # LLM 类型定义
│   └── SseParser.h      # SSE 解析器
│
├── Permission/
│   ├── PermissionGate.h # 权限门控
│   └── PermissionTypes.h# 权限类型
│
├── Records/
│   ├── AgentRecords.h   # 事件记录
│   └── RecordTypes.h    # 事件类型
│
├── Session/
│   ├── Session.h        # 会话管理
│   └── SessionStore.h   # 存储管理
│
└── Tools/
    ├── Bash.h           # Bash 工具
    ├── ReadFile.h       # 读文件工具
    ├── WriteFile.h      # 写文件工具
    └── ...              # 其他工具
```

## 8. 测试结构

```
Test/
├── Agent/
│   └── AgentTest.cpp      # Agent 测试
├── Engine/
│   └── LoopTest.cpp       # Loop 测试
├── Host/
│   └── LocalHostTest.cpp  # Host 测试
├── Llm/
│   ├── OpenAiProviderTest.cpp  # Provider 测试
│   └── SseParserTest.cpp       # SSE 解析测试
├── Permission/
│   └── PermissionTest.cpp # 权限测试
├── Records/
│   └── RecordsTest.cpp    # 记录测试
├── Session/
│   └── SessionTest.cpp    # 会话测试
└── Tools/
    ├── BashTest.cpp       # Bash 工具测试
    └── ToolManagerTest.cpp# 工具管理测试
```

## 9. 小结

本章我们学习了：

- **整体架构**：从底层 Host 到顶层 Agent 的分层设计
- **模块职责**：每个模块负责什么，为什么需要
- **八大设计原则**：Event Sourcing、两阶段执行、无状态循环等
- **依赖方向**：单向依赖，下层不知道上层
- **数据流向**：用户请求如何流转，事件如何分发

## 10. 练习建议

1. **画图**：尝试自己画出完整的架构图，加深理解
2. **阅读代码**：浏览 `Source/CodeHarness/` 目录，建立文件与模块的对应关系
3. **思考题**：为什么 Loop 设计为无状态函数？如果它有内部状态会有什么问题？

## 11. 下一步

下一章我们将深入 **Host 层**，理解文件系统和进程操作的抽象设计。

→ [02-host-layer.md](02-host-layer.md)