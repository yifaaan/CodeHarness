# hooks/ — 钩子系统模块

## 设计目标

在 agent 循环的关键节点插入可扩展的行为，实现"订阅-通知"模式。钩子可用于日志、审计、拦截、修改行为等场景。

## 架构

```
HookRegistry                            ← 按事件类型注册钩子
  └─ HookExecutor::execute(event, payload)
       └─ for each matching hook:
            ├─ CallbackHook  → 调用 C++ 函数
            ├─ CommandHook   → 执行系统命令
            ├─ HttpHook      → HTTP 请求
            ├─ PromptHook    → 注入 prompt
            └─ AgentHook     → 委托给子 agent
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `HookEvent` | 事件枚举：SessionStart/End、PreCompact、PreToolUse、PostToolUse 等 |
| `HookType` | 钩子种类：Callback / Command / Http / Prompt / Agent |
| `HookDefinition` | 钩子定义：关联的事件、类型、过滤器、优先级 |
| `HookResult` | 执行结果，包含 block/allow 决策 |
| `HookRegistry` | 按事件类型存储钩子，按注册顺序排序 |
| `HookExecutor` | 执行匹配事件的钩子，聚合结果 |

## 设计要点

- 钩子按优先级排序，同一事件可以挂载多个钩子
- `HookExecutionResult` 支持 `block` 决策——钩子可以阻止工具执行
- 事件 payload 以 JSON 格式传递，保证通用性

## 初学者指南

- 钩子是扩展点，不是核心循环的必要部分
- 如果你想在工具执行前后添加自定义逻辑，这就是正确的位置
- 核心流程：`Engine` → `HookExecutor::execute(PreToolUse, payload)` → 检查是否被阻止 → 执行工具 → `HookExecutor::execute(PostToolUse, payload)`
