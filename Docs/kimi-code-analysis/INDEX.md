# Kimi Code 架构分析文档

本文档基于对 kimi-code-842e699a643d8a60647bd824d28255c56ad61a42 源码的深度分析，作为 CodeHarness 项目重构的设计参考。

## 文档结构

```
kimi-code-analysis/
├── INDEX.md                    # 本文件 — 文档索引
├── architecture-overview.md    # 系统架构概述
├── core-components.md          # 核心组件详解
├── design-patterns.md          # 设计模式分析
├── implementation-guide.md     # 重构实现指南
└── comparison-with-codeharness.md  # 与当前项目的对比
```

## 核心发现

### 1. 架构特点
- **Monorepo 结构**：pnpm workspace，清晰的包边界
- **分层架构**：CLI/TUI → SDK → Agent Core → Infrastructure
- **事件驱动**：30+ AgentEvent 类型，RPC 隔离
- **无状态循环**：runTurn() 函数无隐藏状态

### 2. 关键设计模式
- **Event Sourcing**：所有状态变更记录为追加事件
- **Two-Phase Tool Execution**：纯验证 + 副作用执行
- **Progressive Disclosure**：文档从高层到详细组织
- **Kaos Abstraction**：统一的文件系统/进程抽象

### 3. 重构价值点
- **ToolScheduler**：基于资源访问冲突的工具并发调度
- **PermissionManager**：三级权限模式（manual/yolo/auto）
- **HookEngine**：13 种钩子事件，支持外部命令执行
- **Context Compaction**：LLM 驱动的上下文压缩

## 重构路线图

基于 OpenAI 文章原则的重构阶段：

| 阶段 | 目标 | 关键模块 |
|------|------|----------|
| **Phase 1** | 基础抽象层 | Kaos → Config → Kosong |
| **Phase 2** | Agent 核心引擎 | Loop → Agent → Context → Turn |
| **Phase 3** | 服务层 | Records → Session → Tools → Permission/Hooks |
| **Phase 4** | 扩展系统 | Skills → MCP |
| **Phase 5** | 应用层 | CLI/TUI → SDK |

## 参考资源

- [Kimi Code 源码](D:\code\kimi-code-842e699a643d8a60647bd824d28255c56ad61a42\kimi-code-842e699a643d8a60647bd824d28255c56ad61a42)
- [OpenAI Codex 工程实践](https://openai.com/index/codex-in-an-agent-first-world/)
- [CodeHarness 当前架构](../plan/re-build/ARCHITECTURE.md)
