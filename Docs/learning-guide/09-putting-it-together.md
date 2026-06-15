# 第9章：串联实战 — 完整请求旅程

> 跟踪一次完整的请求，理解所有模块如何协作。

## 1. 情景设定

**用户输入**：`"帮我看看当前目录的文件结构"`

**预期流程**：
1. Agent 接收请求
2. LLM 分析意图，决定调用 Bash 工具执行 `ls -la`
3. 权限系统询问用户是否允许
4. 用户允许，执行命令
5. 返回结果给 LLM
6. LLM 分析结果，生成回答

## 2. 初始化阶段

### 2.1 创建依赖

```cpp
// ===== 创建 Host =====
auto host = std::make_unique<LocalHost>("/home/user/project");

// ===== 创建 LLM Provider =====
auto provider = std::make_unique<OpenAiProvider>(
    OpenAiConfig{
        .apiKey = "sk-...",
        .model = "gpt-4"
    },
    httpClient.get()
);

// ===== 创建 ToolManager =====
auto toolManager = std::make_unique<ToolManager>();
toolManager->Register(std::make_unique<BashTool>());
toolManager->Register(std::make_unique<ReadFileTool>());
toolManager->Register(std::make_unique<WriteFileTool>());
toolManager->Register(std::make_unique<GlobTool>());
toolManager->Register(std::make_unique<GrepTool>());

// ===== 创建 Agent =====
auto agent = std::make_unique<Agent>(
    provider.get(),
    host.get(),
    toolManager.get()
);

// ===== 设置权限 =====
agent->SetPermissionMode(PermissionMode::Manual);
agent->SetApprovalCallback([](auto toolName, auto args, auto description) {
    std::cout << "=== Permission Request ===\n";
    std::cout << "Tool: " << toolName << "\n";
    std::cout << "Description: " << description << "\n";
    std::cout << "Allow? [y/N]: ";
    char c;
    std::cin >> c;
    return c == 'y' ? PermissionDecision::Allow : PermissionDecision::Deny;
});

// ===== 设置记录 =====
auto records = std::make_unique<FileAgentRecords>("/path/to/wire.jsonl");
agent->SetRecords(records.get());
```

### 2.2 依赖关系图

```
┌─────────────┐
│   Host      │  ← Agent 持有指针
│ (LocalHost) │
└─────────────┘

┌─────────────┐
│  Provider   │  ← Agent 持有指针
│(OpenAiProv.)│
└─────────────┘

┌─────────────┐
│ToolManager  │  ← Agent 持有指针
│  (owns)     │
│ ┌─────────┐ │
│ │BashTool │ │
│ │ReadTool │ │
│ │WriteTool│ │
│ │...      │ │
│ └─────────┘ │
└─────────────┘

┌─────────────┐
│   Agent     │  ← 用户直接使用
│             │
│ - history   │  ← 对话历史
│ - permGate  │  ← 权限门控
│ - records   │  ← 事件记录
└─────────────┘
```

## 3. Prompt 调用

### 3.1 入口

```cpp
// 用户调用
auto result = agent->Prompt("帮我看看当前目录的文件结构");
```

### 3.2 Agent::Prompt 内部

```
Agent::Prompt("帮我看看当前目录的文件结构")
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 1. 状态检查                                                         │
│    if (status == Running) → error                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. 生成 Turn ID                                                     │
│    turnId = "turn_1"                                                │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. 创建 stop_source                                                 │
│    currentStopSource = stop_source{}                                │
│    status = Running                                                 │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 4. 发送事件                                                         │
│    Dispatch(TurnStartedEvent{turnId})                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 5. 记录事件                                                         │
│    records->Log(TurnPrompt, {turnId, input, origin})                │
│    写入 wire.jsonl:                                                  │
│    {"meta":{"ts":...},"record":{"TurnPrompt":{...}}}                 │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 6. 构建用户消息                                                      │
│    userMsg.role = User                                              │
│    userMsg.content = [TextPart{text}]                               │
│    history.push(userMsg)                                            │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 7. 记录消息                                                         │
│    records->Log(ContextAppendMessage, {message})                    │
│    写入 wire.jsonl                                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 8. 构建 TurnInput                                                   │
│    input.provider = provider                                        │
│    input.tools = BuildLoopTools() → [Bash*, Read*, ...]             │
│    input.host = host                                                │
│    input.history = history                                          │
│    input.permissionGate = permissionGate.get()                      │
│    input.stopToken = currentStopSource.get_token()                  │
│    input.dispatchEvent = λ(event) {                                 │
│        Dispatch(LoopEvent{event});                                  │
│        records->Log(ContextAppendLoopEvent, {event});               │
│    }                                                                │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 9. 调用 Loop                                                        │
│    RunTurn(input, {})                                               │
└─────────────────────────────────────────────────────────────────────┘
```

## 4. Loop 第一轮

### 4.1 主循环开始

```
RunTurn(input, {})
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ step = 1                                                            │
│ maxSteps = 1000                                                     │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 1. 检查取消                                                         │
│    stopToken.stop_requested() → false                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. 发送事件                                                         │
│    Dispatch(StepStartedEvent{step=1})                               │
│    → 用户看到: "Step 1 started"                                      │
│    → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. 调用 LLM                                                         │
│    provider->Generate(systemPrompt, tools, history, callbacks)      │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.2 LLM 调用

```
OpenAiProvider::Generate(...)
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 1. 构建 HTTP 请求                                                   │
│    body = {                                                         │
│      "model": "gpt-4",                                              │
│      "messages": [                                                  │
│        {"role": "system", "content": systemPrompt},                 │
│        {"role": "user", "content": "帮我看看当前目录..."}             │
│      ],                                                             │
│      "tools": [                                                     │
│        {"type": "function", "function": {"name": "Bash", ...}},     │
│        {"type": "function", "function": {"name": "Read", ...}},     │
│        ...                                                          │
│      ],                                                             │
│      "stream": true                                                 │
│    }                                                                │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. 发送 HTTPS 请求                                                  │
│    POST https://api.openai.com/v1/chat/completions                  │
│    Headers: Authorization: Bearer sk-...                            │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. 接收 SSE 流                                                      │
│    data: {"choices":[{"delta":{"content":"让我"}}]}                 │
│    data: {"choices":[{"delta":{"content":"先看看"}}]}               │
│    data: {"choices":[{"delta":{"content":"目录..."}}]}              │
│    data: {"choices":[{"delta":{"tool_calls":[...]}}]}               │
│    ...                                                              │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.3 流式回调

```
LLM 返回 SSE 流
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onText("让我")                                                      │
│   → assistantText += "让我"                                         │
│   → Dispatch(AssistantDeltaEvent{"让我"})                           │
│   → 用户看到: "让我"                                                 │
│   → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onText("先看看")                                                    │
│   → assistantText += "先看看"                                       │
│   → Dispatch(AssistantDeltaEvent{"先看看"})                         │
│   → 用户看到: "让我先看看"                                           │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onToolCallStart(0, "call_123", "Bash")                              │
│   → pendingCalls.resize(1)                                          │
│   → pendingCalls[0].id = "call_123"                                 │
│   → pendingCalls[0].name = "Bash"                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onToolCallDelta(0, "{\"com")                                        │
│   → pendingCalls[0].arguments += "{\"com"                           │
│                                                                      │
│ onToolCallDelta(0, "mand\":")                                       │
│   → pendingCalls[0].arguments += "mand\":"                          │
│                                                                      │
│ onToolCallDelta(0, "\"ls -la\"}")                                   │
│   → pendingCalls[0].arguments += "\"ls -la\"}"                      │
│   → 最终: "{\"command\":\"ls -la\"}"                                │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onFinish(ToolCalls, usage)                                          │
│   → finishReason = ToolCalls                                        │
│   → stepUsage = usage                                               │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.4 构建助手消息

```
LLM 流结束
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 构建助手消息                                                         │
│   assistantMsg.role = Assistant                                     │
│   assistantMsg.content = [TextPart{"让我先看看目录..."}]             │
│   assistantMsg.toolCalls = [ToolCall{                               │
│     id: "call_123",                                                 │
│     name: "Bash",                                                   │
│     arguments: "{\"command\":\"ls -la\"}"                           │
│   }]                                                                │
│   history.push(assistantMsg)                                        │
│                                                                      │
│ 记录到 wire.jsonl                                                   │
│   records->Log(ContextAppendMessage, {message})                     │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 发送事件                                                             │
│   Dispatch(StepCompletedEvent{step=1})                              │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 检查是否结束                                                         │
│   finishReason == ToolCalls && hasToolCalls → 继续执行工具           │
└─────────────────────────────────────────────────────────────────────┘
```

## 5. 工具执行

### 5.1 执行 Bash 工具

```
执行工具调用
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 1. 解析参数                                                         │
│    args = json::parse("{\"command\":\"ls -la\"}")                   │
│    → {"command": "ls -la"}                                          │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. 发送事件                                                         │
│    Dispatch(ToolCallStartedEvent{"call_123", "Bash", args})         │
│    → 用户看到: "Tool Bash started"                                   │
│    → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. 查找工具                                                         │
│    tool = FindTool(input.tools, "Bash") → BashTool*                 │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 4. ResolveExecution                                                 │
│    resolution = bashTool.ResolveExecution(args)                     │
│    → ToolExecution{                                                 │
│         description: "Execute: ls -la",                             │
│         requiresPermission: true                                    │
│       }                                                             │
└─────────────────────────────────────────────────────────────────────┘
```

### 5.2 权限检查

```
requiresPermission == true && permissionGate != null
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 5. 发送权限请求事件                                                  │
│    Dispatch(PermissionRequestedEvent{"Bash", args, "Execute: ls -la"})│
│    → 用户看到: "=== Permission Request ==="                          │
│               "Tool: Bash"                                           │
│               "Description: Execute: ls -la"                         │
│               "Allow? [y/N]: "                                       │
│    → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 6. 权限门控检查                                                      │
│    shouldRun = permissionGate.ShouldRun(                            │
│        true, "Bash", args, "Execute: ls -la"                        │
│    )                                                                │
│                                                                      │
│    PermissionGate::ShouldRun 内部:                                  │
│    - requiresPermission == true → 需要检查                          │
│    - mode == Manual → 调用回调                                       │
│    - callback("Bash", args, "Execute: ls -la") → 用户输入 'y'        │
│    - 返回 Allow → true                                              │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 7. 用户允许 → 继续执行                                               │
└─────────────────────────────────────────────────────────────────────┘
```

### 5.3 Execute 执行

```
用户允许 → Execute(args, ctx)
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ BashTool::Execute({"command": "ls -la"}, ctx)                       │
│                                                                      │
│ 1. 使用 Host 执行命令                                                │
│    process = ctx.host->Exec("ls -la")                               │
│    → 启动进程                                                        │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 2. Drain 输出                                                       │
│    result = process->Drain(60000, ctx.stopToken)                    │
│                                                                      │
│    Drain 内部:                                                       │
│    - 同时读取 stdout 和 stderr                                       │
│    - 等待进程结束或超时                                               │
│    - 返回 {out, err, exitCode, finished}                            │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 3. 截断输出                                                         │
│    content = TruncateOutput(result.out + result.err)                │
│    → 如果超过 50000 字符，截断                                       │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 4. 返回结果                                                         │
│    return ToolResult{                                               │
│        content: "drwxr-xr-x  ...",                                  │
│        isError: result.exitCode != 0                                │
│    }                                                                │
└─────────────────────────────────────────────────────────────────────┘
```

### 5.4 构建工具消息

```
工具执行完成 → ToolResult
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 发送结果事件                                                         │
│    Dispatch(ToolResultEvent{"call_123", "Bash", result})            │
│    → 用户看到: "Tool result: ..."                                    │
│    → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 构建工具消息                                                         │
│    toolMsg.role = Tool                                              │
│    toolMsg.toolCallId = "call_123"                                  │
│    toolMsg.content = [TextPart{result.content}]                     │
│    history.push(toolMsg)                                            │
│                                                                      │
│ 记录到 wire.jsonl                                                   │
│    records->Log(ContextAppendMessage, {message})                    │
└─────────────────────────────────────────────────────────────────────┘
```

## 6. Loop 第二轮

### 6.1 再次调用 LLM

```
工具执行完成 → history 已更新
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ step = 2                                                            │
│                                                                      │
│ Dispatch(StepStartedEvent{step=2})                                  │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ provider->Generate(..., history, ...)                               │
│                                                                      │
│ history 现在:                                                        │
│ [User: "帮我看看...",                                                │
│  Assistant: "让我先看看目录..." + toolCalls,                         │
│  Tool: "drwxr-xr-x ..."]                                            │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ LLM 分析工具结果                                                     │
│   → 决定：分析结果，生成回答                                         │
│   → 返回文本，不调用工具                                             │
│                                                                      │
│ SSE 流:                                                              │
│   data: {"delta":{"content":"当前目录包含..."}}                      │
│   data: {"delta":{"content":"主要文件有..."}}                        │
│   ...                                                              │
│   data: {"delta":{},"finish_reason":"stop"}                         │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onText 回调                                                         │
│   → 用户看到: "当前目录包含...主要文件有..."                          │
│   → 记录到 wire.jsonl                                               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ onFinish(Completed, usage)                                          │
│   → finishReason = Completed                                       │
└─────────────────────────────────────────────────────────────────────┘
```

### 6.2 构建最终助手消息

```
LLM 完成 → 构建消息
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 构建助手消息                                                         │
│    assistantMsg.role = Assistant                                    │
│    assistantMsg.content = [TextPart{"当前目录包含...主要文件有..."}] │
│    assistantMsg.toolCalls = []                                      │
│    history.push(assistantMsg)                                       │
│                                                                      │
│ 记录到 wire.jsonl                                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Dispatch(StepCompletedEvent{step=2})                                │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 检查是否结束                                                         │
│    finishReason == Completed && !hasToolCalls → 结束               │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 返回 TurnResult                                                     │
│    stopReason = Completed                                           │
│    stepsExecuted = 2                                                │
│    updatedHistory = [...所有消息...]                                │
└─────────────────────────────────────────────────────────────────────┘
```

## 7. Agent 处理结果

### 7.1 返回给用户

```
Loop 返回 TurnResult → Agent::Prompt 继续
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 10. 更新历史                                                         │
│     history = loopResult.updatedHistory                             │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 11. 设置状态                                                         │
│     status = Idle                                                   │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 12. 发送事件                                                         │
│     Dispatch(TurnEndedEvent{result})                                │
│     → 用户看到: "Turn ended"                                         │
│     → 记录到 wire.jsonl                                              │
└─────────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────────┐
│ 13. 返回 PromptResult                                               │
│     return PromptResult{                                            │
│         turnId: "turn_1",                                           │
│         stopReason: Completed,                                      │
│         stepsExecuted: 2,                                           │
│         usage: {...},                                               │
│         errorMessage: ""                                            │
│     }                                                               │
└─────────────────────────────────────────────────────────────────────┘
```

### 7.2 用户收到结果

```cpp
// 用户代码
auto result = agent->Prompt("帮我看看当前目录的文件结构");

if (result.ok()) {
    std::cout << "Turn ID: " << result->turnId << "\n";
    std::cout << "Steps: " << result->stepsExecuted << "\n";
    std::cout << "Stop reason: " << result->stopReason << "\n";
    
    // 获取完整历史
    auto& history = agent->GetHistory();
    // history 包含:
    // [User: "帮我看看...", 
    //  Assistant: "让我先看看目录..." + toolCalls,
    //  Tool: "drwxr-xr-x ...",
    //  Assistant: "当前目录包含...主要文件有..."]
}
```

## 8. wire.jsonl 最终内容

```jsonl
{"meta":{"ts":1700000000000,"protocol":"1.0"},"record":{"TurnPrompt":{"turnId":"turn_1","input":[{"Text":{"text":"帮我看看当前目录的文件结构"}}],"origin":0}}}
{"meta":{"ts":1700000000100,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"User","content":[{"Text":{"text":"帮我看看当前目录的文件结构"}}]}}}}
{"meta":{"ts":1700000001000,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"StepStarted":{"step":1}}}}}
{"meta":{"ts":1700000001500,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"AssistantDelta":{"text":"让我"}}}}}
{"meta":{"ts":1700000001600,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"AssistantDelta":{"text":"先看看"}}}}}
{"meta":{"ts":1700000001700,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"AssistantDelta":{"text":"目录..."}}}}}
{"meta":{"ts":1700000002000,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"Assistant","content":[{"Text":{"text":"让我先看看目录..."}}],"toolCalls":[{"id":"call_123","name":"Bash","arguments":"{\"command\":\"ls -la\"}"}]}}}}
{"meta":{"ts":1700000002100,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"StepCompleted":{"step":1}}}}}
{"meta":{"ts":1700000002200,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"ToolCallStarted":{"id":"call_123","name":"Bash","args":{"command":"ls -la"}}}}}}
{"meta":{"ts":1700000002300,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"PermissionRequested":{"toolName":"Bash","args":{"command":"ls -la"},"description":"Execute: ls -la"}}}}}
{"meta":{"ts":1700000003000,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"ToolResult":{"id":"call_123","name":"Bash","result":{"content":"drwxr-xr-x ...","isError":false}}}}}}
{"meta":{"ts":1700000003100,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"Tool","content":[{"Text":{"text":"drwxr-xr-x ..."}}],"toolCallId":"call_123"}}}}
{"meta":{"ts":1700000004000,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"StepStarted":{"step":2}}}}}
{"meta":{"ts":1700000004500,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"AssistantDelta":{"text":"当前目录包含..."}}}}}
{"meta":{"ts":1700000004600,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"AssistantDelta":{"text":"主要文件有..."}}}}}
{"meta":{"ts":1700000005000,"protocol":"1.0"},"record":{"ContextAppendMessage":{"message":{"role":"Assistant","content":[{"Text":{"text":"当前目录包含...主要文件有..."}}]}}}}
{"meta":{"ts":1700000005100,"protocol":"1.0"},"record":{"ContextAppendLoopEvent":{"event":{"StepCompleted":{"step":2}}}}}
{"meta":{"ts":1700000006000,"protocol":"1.0"},"record":{"TurnEnded":{"result":{"turnId":"turn_1","stopReason":0,"stepsExecuted":2}}}}
```

## 9. 时序图

```
用户          Agent        Loop        Provider      PermissionGate   BashTool      Host        wire.jsonl
  │            │            │            │                │              │            │            │
  │ Prompt()   │            │            │                │              │            │            │
  │───────────>│            │            │                │              │            │            │
  │            │ Log(TurnPrompt)         │                │              │            │            │───────────>
  │            │            │            │                │              │            │            │
  │            │ RunTurn()  │            │                │              │            │            │
  │            │───────────>│            │                │              │            │            │
  │            │            │ Generate() │                │              │            │            │
  │            │            │───────────>│                │              │            │            │
  │            │            │            │ HTTP request   │              │            │            │
  │            │            │            │────────────────────────────────────────────>│            │
  │            │            │            │                │              │            │            │
  │            │            │            │ SSE stream     │              │            │            │
  │            │            │            │<────────────────────────────────────────────│            │
  │            │            │ onText()   │                │              │            │            │
  │            │            │<───────────│                │              │            │            │
  │            │            │ Log(AssistantDelta)        │              │            │            │───────────>
  │            │            │            │                │              │            │            │
  │            │            │ onToolCallStart/Delta      │              │            │            │
  │            │            │<───────────│                │              │            │            │
  │            │            │            │                │              │            │            │
  │            │            │ ResolveExecution()         │              │            │            │
  │            │            │───────────────────────────────────────────>│            │            │
  │            │            │            │                │              │ ToolExecution│            │
  │            │            │            │                │              │<───────────│            │
  │            │            │            │                │              │            │            │
  │            │            │ ShouldRun()│                │              │            │            │
  │            │            │────────────────────────────────────────────>│            │            │
  │            │            │            │                │ callback()   │            │            │
  │<───────────────────────────────────────────────────────│            │            │            │
  │ "Allow?"   │            │            │                │              │            │            │
  │────────────────────────────────────────────────────────────────────>│            │            │
  │            │            │            │                │ Allow        │            │            │
  │            │            │            │                │<─────────────│            │            │
  │            │            │            │                │              │            │            │
  │            │            │ Execute()  │                │              │            │            │
  │            │            │───────────────────────────────────────────>│            │            │
  │            │            │            │                │              │ Exec()     │            │
  │            │            │            │                │              │───────────>│            │
  │            │            │            │                │              │            │ Drain()    │
  │            │            │            │                │              │            │───────────>│
  │            │            │            │                │              │            │            │
  │            │            │ ToolResult │                │              │            │            │
  │            │            │<───────────────────────────────────────────│            │            │
  │            │            │ Log(ToolResult)            │              │            │            │───────────>
  │            │            │            │                │              │            │            │
  │            │            │ (step 2)   │                │              │            │            │
  │            │            │ Generate() │                │              │            │            │
  │            │            │───────────>│                │              │            │            │
  │            │            │            │ SSE stream     │              │            │            │
  │            │            │ onText()   │                │              │            │            │
  │            │            │<───────────│                │              │            │            │
  │            │            │            │                │              │            │            │
  │            │ TurnResult │            │                │              │            │            │
  │            │<───────────│            │                │              │            │            │
  │            │            │            │                │              │            │            │
  │ PromptResult│            │            │                │              │            │            │
  │<───────────│            │            │                │              │            │            │
  │            │            │            │                │              │            │            │
```

## 10. 总结

本章我们跟踪了一次完整请求的旅程：

**初始化阶段**：
- 创建 Host、Provider、ToolManager、Agent
- 设置权限回调、事件记录

**Prompt 调用**：
- Agent 检查状态、生成 Turn ID
- 记录事件、构建 TurnInput

**Loop 第一轮**：
- 调用 LLM，接收流式响应
- LLM 决定调用 Bash 工具
- 权限检查 → 用户允许
- 执行工具 → Host.Exec → Drain

**Loop 第二轮**：
- LLM 分析工具结果
- 生成最终回答
- 返回 TurnResult

**结果处理**：
- Agent 更新历史、设置状态
- 返回 PromptResult

**事件记录**：
- 所有操作记录到 wire.jsonl
- 支持恢复和回放

## 11. 调试技巧

### 11.1 跟踪事件

```cpp
agent->SetEventDispatcher([](const AgentEvent& event) {
    std::visit([](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, AssistantDeltaEvent>) {
            spdlog::info("Assistant: {}", e.text);
        } else if constexpr (std::is_same_v<T, ToolCallStartedEvent>) {
            spdlog::info("Tool: {} with args {}", e.name, e.args.dump());
        }
        // ...
    }, event);
});
```

### 11.2 查看 wire.jsonl

```bash
# 查看所有记录
cat ~/.codeharness/sessions/<workdir-key>/<session-id>/agents/main/wire.jsonl

# 过滤特定类型
grep "ToolCallStarted" wire.jsonl
```

### 11.3 Mock 测试

```cpp
// Mock Provider，返回固定响应
MockChatProvider provider;
provider.responses = {
    {.toolCalls = {ToolCall{"1", "Bash", "{\"command\":\"ls\"}"}}},
    {.text = "Done"}
};

// Mock Tool，返回固定结果
MockTool bash;
bash.executeHandler = [](auto) { return ToolResult{"mock output"}; };

// 测试
Agent agent(&provider, nullptr, &tools);
auto result = agent.Prompt("test");
```

## 12. 下一步

恭喜你完成了整个学习指南！现在你应该对 CodeHarness 的架构有了深入理解。

**建议的下一步**：
1. 阅读源代码，对照文档加深理解
2. 实现一个简单的工具，体验工具开发
3. 添加一个新 Provider，理解 LLM 抽象
4. 参考测试代码，学习测试技巧

**参考文档**：
- [AGENTS.md](../../AGENTS.md) — 项目总览
- [docs/plan/re-build/](../plan/re-build/) — 详细设计文档
- [docs/coding-conventions.md](../coding-conventions.md) — 编码规范

祝你学习愉快！