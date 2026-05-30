# Engine 模块设计分析

Engine 是 OpenHarness 的核心，负责把一次用户输入变成完整的 agent loop。

上游关键文件：

- `docs/OpenHarness/src/openharness/engine/query.py`
- `docs/OpenHarness/src/openharness/engine/query_engine.py`
- `docs/OpenHarness/src/openharness/engine/messages.py`
- `docs/OpenHarness/src/openharness/engine/stream_events.py`
- `docs/OpenHarness/src/openharness/engine/cost_tracker.py`

## 模块职责

Engine 做这些事：

1. 保存对话历史。
2. 接收用户 prompt。
3. 调用 provider streaming API。
4. 把 provider streaming event 转成内部 stream event。
5. 识别 assistant 返回的 tool calls。
6. 执行工具调用。
7. 把工具结果追加到消息历史。
8. 继续下一轮模型调用。
9. 处理 max turns、prompt too long、auto compact、usage cost。

Engine 不应该直接关心 UI 如何显示，也不应该直接知道 OpenAI 或 Anthropic 的 wire format。它只依赖统一的 API client 接口。

## QueryEngine

`QueryEngine` 是会话级对象。它持有：

- `api_client`
- `tool_registry`
- `permission_checker`
- `cwd`
- `model`
- `system_prompt`
- `messages`
- `cost_tracker`
- `tool_metadata`

用户每输入一条普通文本，会调用：

```text
QueryEngine.submit_message(line)
```

它会把文本封装为：

```text
ConversationMessage(role="user", content=[TextBlock(text=line)])
```

然后调用 `run_query()`。

## QueryContext

`QueryContext` 是一次 query loop 的依赖集合。上游包含：

- `api_client`
- `tool_registry`
- `permission_checker`
- `cwd`
- `model`
- `system_prompt`
- `max_tokens`
- `effort`
- `context_window_tokens`
- `auto_compact_threshold_tokens`
- `permission_prompt`
- `ask_user_prompt`
- `max_turns`
- `hook_executor`
- `tool_metadata`

初学者可以把它理解成“执行这一轮 agent loop 所需的上下文参数”。

## 消息模型

OpenHarness 内部用接近 Anthropic 的 block message 模型。

### ConversationMessage

一条消息有角色和内容块：

```text
ConversationMessage
  role: user | assistant
  content: list[ContentBlock]
```

### ContentBlock

常见内容块：

- `TextBlock`：普通文本。
- `ImageBlock`：图片，包含 media type、base64 数据、来源路径。
- `ToolUseBlock`：assistant 请求调用工具。
- `ToolResultBlock`：工具执行结果。

为什么用 block？因为一条 assistant 消息可能同时包含普通文本和多个工具调用。

## StreamEvent

Engine 不直接输出字符串，而是输出事件：

| 事件 | 含义 |
| --- | --- |
| `AssistantTextDelta` | 模型流式输出片段 |
| `AssistantTurnComplete` | assistant 当前轮完成 |
| `ToolExecutionStarted` | 工具开始执行 |
| `ToolExecutionCompleted` | 工具完成 |
| `StatusEvent` | 状态提示，如 retry、compact |
| `ErrorEvent` | 错误 |
| `CompactProgressEvent` | 上下文压缩进度 |

这样 UI、CLI、JSON 输出都可以共用 engine。

## run_query 核心流程

简化流程：

```text
turn = 0
while true:
    if max_turns exceeded:
        emit error
        break

    maybe auto compact
    maybe preprocess images

    request = ApiMessageRequest(
        model,
        messages,
        system_prompt,
        tools=tool_registry.to_api_schema()
    )

    assistant_message = stream provider response
    append assistant_message
    emit AssistantTurnComplete

    tool_uses = extract tool use blocks
    if no tool_uses:
        break

    tool_results = execute all tool_uses
    append ConversationMessage(role=user, content=tool_results)
```

## 工具执行流程

`_execute_tool_call()` 做的事非常多，是安全边界核心：

1. 触发 `PRE_TOOL_USE` hook。
2. 从 `ToolRegistry` 查找工具。
3. 用工具的 input model 校验参数。
4. 提取权限目标，例如 path、command。
5. 调用 `PermissionChecker.evaluate()`。
6. 如果需要确认，调用 UI 提供的 `permission_prompt()`。
7. 执行 `tool.execute()`。
8. 对大输出做截断或 offload。
9. 更新 `tool_metadata`。
10. 触发 `POST_TOOL_USE` hook。
11. 生成 `ToolResultBlock`。

工具执行失败时，不应该抛出到整个 agent loop 外面，而应该返回 `ToolResultBlock`，其中 `is_error=true`。这样模型能看到失败原因并尝试修正。

## 并发工具调用

模型可能一次返回多个工具调用。例如同时读取多个文件。Python 版用 `asyncio.gather()` 并发执行。

规则：

- 单个工具调用可以顺序执行。
- 多个工具调用可以并发。
- 无论成功失败，每个 `tool_use_id` 都必须有对应 `tool_result`。
- 回填给模型时应保持原 tool call 顺序，减少 provider 兼容问题。

## Tool metadata

Engine 会把一些工具执行结果记到 `tool_metadata`，用于后续 prompt 或 UI 状态：

- 最近读取的文件。
- 已加载的 skills。
- 最近 agent 任务。
- 最近工作日志。
- 计划模式状态。
- 当前目标和 active artifacts。

C++20 初版可以简化，但建议保留一个 JSON metadata map，避免后续扩展困难。

## Auto compact

当上下文太长时，OpenHarness 会尝试压缩历史，避免 provider 报 prompt too long。初版可以先不实现自动压缩，但必须保留：

- `max_turns` 防无限循环。
- provider 报 context length 错误时给出清晰错误。
- 工具输出截断，减少上下文爆炸。

## 图像预处理

上游支持 `ImageBlock`。如果当前模型不支持多模态，engine 会尝试调用 `image_to_text` 工具先把图片转文字。

C++20 初版可以暂时不做图片，消息模型仍建议预留 `ImageBlock`，避免未来大改。

## Engine 测试重点

重写时应优先写这些测试：

- 普通文本 prompt 能得到 assistant delta。
- 模型返回一个 tool use，engine 执行工具并回填结果。
- 工具抛异常时返回 `is_error=true`。
- 多工具调用能都返回结果。
- max turns 超过后停止。
- provider streaming 中断时返回错误事件。
- 权限拒绝时工具不执行。
- 大工具输出被截断或保存 artifact。
