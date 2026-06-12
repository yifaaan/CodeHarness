# Thinking 与模型能力计划

## 目标

为模型 profile 增加能力描述和 thinking 参数，让 runtime 能根据模型能力决定工具、多模态输入和推理预算。

## 当前缺口

- `ProviderProfile` 当前主要包含 provider、model、base_url、auth_source。
- 没有 `capabilities`、`thinking`、`max_context_size`、`max_output_size` 等用户配置。
- 工具注册目前不按模型能力动态裁剪视觉或音频能力。

## 建议落点

- `src/codeharness/config/config.*`：扩展 profile schema。
- `src/codeharness/runtime/runtime.*`：把能力解析为 RuntimeModelProfile 的一部分。
- `src/codeharness/provider/`：按 provider 序列化 thinking 参数。
- `src/codeharness/tui/`：模型选择器展示能力标签。

## 验收标准

- 配置可声明模型能力。
- TUI 模型列表展示能力摘要。
- Provider 请求只发送该供应商支持的 thinking 字段。
- 不支持的能力不会静默假装可用。
