# CodeHarness 深度学习指南

> 面向有软件工程经验的开发者，系统性地讲解 AI Coding Agent 的架构与实现。

## 这是什么？

CodeHarness 是一个用 **C++20** 实现的 AI Coding Agent 框架。它可以：
- 接入 OpenAI、Anthropic 等 LLM Provider
- 让 AI Agent 读取代码、编辑文件、执行命令
- 通过权限系统控制风险操作
- 持久化会话，支持恢复和回放

本指南将带你**从零理解**整个系统的设计与实现。

## 目标读者

- 有 C++ 或其他语言的后端开发经验
- 了解面向对象设计、设计模式
- 对 LLM Agent 开发感兴趣，但可能是初次接触

## 学习路径

```
┌─────────────────────────────────────────────────────────────────┐
│  第0章: 前置知识                                                 │
│  了解 LLM Agent 的核心概念：Message、ToolCall、Streaming         │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  第1章: 架构总览                                                 │
│  理解整体分层设计、8大设计原则、模块职责                          │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  第2-8章: 逐层深入                                               │
│  Host → LLM → Tool → Loop → Agent → Permission → Session        │
│  每章包含：概念讲解 + 接口解读 + 代码导读 + 测试分析              │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  第9章: 串联实战                                                 │
│  跟踪一次完整请求的旅程，理解各模块如何协作                        │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  第10-12章: 扩展模块                                             │
│  Context（上下文压缩） + Hooks（生命周期钩子） + Skills（技能）  │
└─────────────────────────────────────────────────────────────────┘
```

## 章节目录

| 章节 | 文件 | 核心内容 | 预计用时 |
|------|------|----------|----------|
| 第0章 | [00-prerequisites.md](00-prerequisites.md) | LLM Agent 核心概念 | 20分钟 |
| 第1章 | [01-architecture-overview.md](01-architecture-overview.md) | 系统架构与设计原则 | 30分钟 |
| 第2章 | [02-host-layer.md](02-host-layer.md) | Host层：文件系统与进程抽象 | 40分钟 |
| 第3章 | [03-llm-layer.md](03-llm-layer.md) | LLM层：Provider接口与流式处理 | 50分钟 |
| 第4章 | [04-tool-system.md](04-tool-system.md) | 工具系统：两阶段执行模型 | 40分钟 |
| 第5章 | [05-loop-engine.md](05-loop-engine.md) | Loop引擎：无状态决策循环 | 50分钟 |
| 第6章 | [06-agent-core.md](06-agent-core.md) | Agent核心：组合根与生命周期 | 40分钟 |
| 第7章 | [07-permission-system.md](07-permission-system.md) | 权限系统：安全门控机制 | 30分钟 |
| 第8章 | [08-session-records.md](08-session-records.md) | 会话与事件溯源 | 40分钟 |
| 第9章 | [09-putting-it-together.md](09-putting-it-together.md) | 串联实战：完整请求旅程 | 30分钟 |
| 第10章 | [10-context-memory.md](10-context-memory.md) | Context模块：上下文内存与压缩 | 30分钟 |
| 第11章 | [11-hooks-system.md](11-hooks-system.md) | Hooks模块：生命周期钩子 | 30分钟 |
| 第12章 | [12-skills-system.md](12-skills-system.md) | Skills模块：可复用技能片段 | 30分钟 |

## 每章结构

```
1. 概念讲解
   - 为什么需要这个模块？
   - 解决什么问题？

2. 架构图示
   - 类关系图
   - 数据流图
   - 时序图

3. 接口解读
   - 关键类型定义
   - 方法签名说明

4. 代码导读
   - 带详细中文注释的代码片段
   - 关键行逐一解释

5. 测试分析
   - 对应测试用例解读
   - 测试覆盖的场景

6. 练习建议
   - 动手任务
   - 进阶挑战
```

## 快速开始

### 方式一：按顺序学习（推荐）

如果你是初次接触 LLM Agent 开发，建议按章节顺序学习：

```bash
# 1. 先看前置知识，建立概念基础
docs/learning-guide/00-prerequisites.md

# 2. 了解整体架构
docs/learning-guide/01-architecture-overview.md

# 3. 从底层开始，逐层向上
docs/learning-guide/02-host-layer.md    # 最底层
docs/learning-guide/03-llm-layer.md     # LLM抽象
# ... 继续后续章节
```

### 方式二：按兴趣跳转

如果你已有 LLM Agent 开发经验，可以直接跳到感兴趣的模块：

| 你想了解... | 推荐阅读 |
|-------------|----------|
| 如何抽象文件系统和进程？ | [02-host-layer.md](02-host-layer.md) |
| 如何统一多个LLM Provider？ | [03-llm-layer.md](03-llm-layer.md) |
| 工具执行的权限控制？ | [07-permission-system.md](07-permission-system.md) |
| Agent的核心循环逻辑？ | [05-loop-engine.md](05-loop-engine.md) |
| 会话持久化如何实现？ | [08-session-records.md](08-session-records.md) |
| 上下文压缩如何工作？ | [10-context-memory.md](10-context-memory.md) |
| 如何扩展生命周期钩子？ | [11-hooks-system.md](11-hooks-system.md) |
| 如何复用提示词/工作流片段？ | [12-skills-system.md](12-skills-system.md) |

## 源码导航

```
Source/CodeHarness/
├── Agent/          # 第6章：Agent核心
│   ├── Agent.h
│   ├── Agent.cpp
│   └── AgentTypes.h
├── Config/         # 配置管理
│   ├── Config.h
│   ├── ConfigTypes.h
│   └── ProviderManager.h
├── Context/        # 第10章：上下文内存与压缩
│   ├── ContextMemory.h
│   ├── TokenEstimate.h
│   └── Compactor.h
├── Engine/         # 第4、5章：工具系统 + Loop引擎
│   ├── Tool.h
│   ├── Loop.h
│   ├── Loop.cpp
│   ├── LoopTypes.h
│   └── LoopHooks.h
├── Hooks/          # 第11章：生命周期钩子
│   ├── HookEngine.h
│   └── HookTypes.h
├── Host/           # 第2章：Host层
│   ├── Host.h
│   ├── LocalHost.h
│   ├── HostPath.h
│   └── Environment.h
├── Llm/            # 第3章：LLM层
│   ├── ChatProvider.h
│   ├── OpenAiProvider.h
│   ├── Types.h
│   └── SseParser.h
├── Permission/     # 第7章：权限系统
│   ├── PermissionGate.h
│   └ PermissionTypes.h
├── Records/        # 第8章：事件溯源
│   ├── AgentRecords.h
│   └── RecordTypes.h
├── Session/        # 第8章：会话管理
│   ├── Session.h
│   └── SessionStore.h
├── Skills/         # 第12章：可复用技能片段
│   ├── SkillRegistry.h
│   ├── SkillManager.h
│   └── SkillTool.h
└── Tools/          # 第4章：内置工具
    ├── Bash.h
    ├── ReadFile.h
    ├── WriteFile.h
    └── ...
```

## 技术栈速览

| 领域 | 技术 |
|------|------|
| 语言 | C++20 |
| 构建系统 | CMake + vcpkg |
| JSON处理 | nlohmann::json |
| 错误处理 | absl::Status / StatusOr |
| 日志 | spdlog |
| 格式化 | fmt |
| 测试框架 | GoogleTest |

## 相关文档

- [ARCHITECTURE.md](../../ARCHITECTURE.md) - 系统架构概述
- [docs/plan/re-build/](../plan/re-build/) - 详细设计文档
- [docs/guides/getting-started.md](../guides/getting-started.md) - 快速开始

## 贡献

如果你在学习过程中发现文档有误或有改进建议，欢迎提 issue 或 PR。
