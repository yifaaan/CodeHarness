# provider/ — LLM 提供商接口模块

## 设计目标

为 CodeHarness 提供统一的 LLM 调用抽象，屏蔽不同 API 提供商的差异。所有 LLM 交互都通过此接口进行。

## 架构

```
Provider (抽象接口)
  ├─ stream(messages, options) → ProviderEvent 流
  │    ├─ AssistantTextDelta     ← 文本增量
  │    ├─ ToolUseStarted         ← 工具调用开始
  │    ├─ ToolUseInputDelta      ← 工具输入增量
  │    ├─ ToolUseFinished        ← 工具调用完成
  │    └─ MessageFinished        ← 完整消息完成
  └─ generate(messages, options) → Message (便捷阻塞封装)

EchoProvider
  └─ 默认 provider，回显输入作为 assistant 响应（测试/开发用）
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `Provider` | 抽象基类，定义 `stream()` 和 `generate()` 接口 |
| `ProviderEvent` | 流式事件的 variant 类型 |
| `EchoProvider` | 默认实现，开发测试用 |
| `ProviderEventCollector` | 在 `core/` 模块中，用于将 ProviderEvent 流收拢为 `Message` |

## 设计要点

- **统一消息模型**：所有 LLM 提供商都转换为 `core::Message` 内部格式，Engine 不依赖任何提供商特有格式
- 流式接口采用 `std::function<void(ProviderEvent)>` 回调方式
- `ProviderEventCollector` 将流式事件合并为完整的 `Message`
- 未来可在此基础上实现 OpenAI、Anthropic 等真实提供商

## 初学者指南

- `Provider` 是 CodeHarness 与 LLM 之间的桥梁
- 如果你要实现一个新的 LLM 提供商，继承 `Provider` 并实现 `stream()` 即可
- 当前只有 `EchoProvider` 作为默认实现
- 核心路径：`Engine` → `Provider::stream()` → `ProviderEvent` 回调 → `ProviderEventCollector` 组包
