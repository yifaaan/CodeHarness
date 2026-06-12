# Kimi Code 重构设计文档

本目录包含 Kimi Code CLI（TypeScript）的架构分析文档，作为 CodeHarness（C++20）重构的设计参考。

## 目的

1. **架构分析**：深入理解 Kimi Code 的设计决策和实现细节
2. **重构指南**：为 CodeHarness 重构提供详细的实现计划
3. **最佳实践**：提取可复用的设计模式和架构原则

## 与 CodeHarness 的关系

| 项目 | 语言 | 目标 |
|------|------|------|
| Kimi Code | TypeScript/Node.js | 原始实现 |
| CodeHarness | C++20 | 重构实现 |

本目录的文档描述的是 **Kimi Code** 的架构，而非 CodeHarness 的当前实现。

## 文档层次

```
第 1 层: AGENTS.md（面向 Agent 的入口，~100 行）
    │
    ▼
第 2 层: ARCHITECTURE.md（高层架构，~200 行）
    │
    ▼
第 3 层: design-docs/（设计原则，~100 行/文件）
    │
    ▼
第 4 层: references/（模块参考，~100-150 行/文件）
    │
    ▼
补充层: 01-16*.md（详细实现文档，~300-500 行/文件）
    │
    ▼
生成层: generated/（自动生成的文档，如数据库 Schema）
```

## 快速导航

### 面向 Agent
- [AGENTS.md](AGENTS.md) — 入口文件，包含代码库地图和核心原则

### 面向开发者
- [ARCHITECTURE.md](ARCHITECTURE.md) — 高层架构和设计决策
- [design-docs/core-beliefs.md](design-docs/core-beliefs.md) — 8 条核心设计原则

### 模块参考
- [references/index.md](references/index.md) — 模块参考索引
- [references/kaos-interface.md](references/kaos-interface.md) — 执行环境抽象
- [references/kosong-interface.md](references/kosong-interface.md) — LLM 提供商抽象
- [references/agent-lifecycle.md](references/agent-lifecycle.md) — Agent 生命周期

### 详细文档
- [01-architecture-overview.md](01-architecture-overview.md) — 架构总览
- [02-kaos-execution-layer.md](02-kaos-execution-layer.md) — Kaos 实现细节
- [05-agent-core-engine.md](05-agent-core-engine.md) — Agent 核心引擎

### 执行计划
- [exec-plans/active/](exec-plans/active/) — 当前执行中的计划
- [exec-plans/tech-debt-tracker.md](exec-plans/tech-debt-tracker.md) — 技术债务追踪

## 重构路线图

详见 [ARCHITECTURE.md](ARCHITECTURE.md) 的 Re-implementation Roadmap 章节。

| 阶段 | 模块 | 优先级 |
|------|------|--------|
| **Phase 1: Foundation** | Kaos → Config → Kosong | #1-#3 |
| **Phase 2: Agent Core** | Loop → Agent → Context → Turn | #4-#7 |
| **Phase 3: Services** | Records → Session → Tools → Permission/Hooks | #8-#12 |
| **Phase 4: Extensions** | Skills → MCP | #13-#14 |
| **Phase 5: Application** | CLI/TUI → SDK | #15-#16 |

## 贡献指南

1. **文档更新**：修改代码时同步更新相关文档
2. **引用完整性**：确保所有交叉引用有效
3. **命名规范**：文件名使用 `XX-descriptive-name.md` 格式
4. **内容层次**：保持从高层到详细的渐进式披露

## 参考资源

- [Kimi Code 源码](D:\code\kimi-code-842e699a643d8a60647bd824d28255c56ad61a42\kimi-code-842e699a643d8a60647bd824d28255c56ad61a42)
- [OpenAI 智能体优先工程](https://openai.com/index/codex-in-an-agent-first-world/)
- [CodeHarness 项目](../../)
