# 初学者概念指南

这份文档解释重写 OpenHarness 前需要理解的基础概念，重点面向 agent 开发和网络编程初学者。

## Agent 和 Chatbot 的区别

普通 chatbot 通常只做：

```text
用户输入 -> 模型回答 -> 结束
```

Agent 会多一层循环：

```text
用户输入 -> 模型决定下一步 -> 调用工具 -> 观察结果 -> 再决定下一步 -> 最终回答
```

例如用户说“帮我修复测试失败”：

1. 模型先决定运行测试。
2. harness 调用 `bash` 工具。
3. 模型看到测试输出。
4. 模型决定读取某个文件。
5. harness 调用 `read_file`。
6. 模型决定修改文件。
7. harness 调用 `edit_file`。
8. 模型再运行测试。
9. 测试通过后给用户总结。

这里模型只是做决策，实际读文件、跑命令、改文件都由 harness 完成。

## Tool Call 是什么

Tool call 是模型 API 返回的一种结构化请求。它不是普通文本，而是类似：

```json
{
  "id": "toolu_123",
  "name": "read_file",
  "input": {
    "path": "src/main.cpp",
    "offset": 0,
    "limit": 200
  }
}
```

harness 收到后会：

1. 根据 `name` 找工具。
2. 校验 `input`。
3. 做权限检查。
4. 执行工具。
5. 把结果变成 `tool_result` 回给模型。

## JSON Schema 为什么重要

模型需要知道工具参数长什么样。OpenHarness 里每个工具都有一个输入模型，例如 `read_file` 需要 path、offset、limit。Python 版用 Pydantic 自动生成 JSON Schema。

C++20 版可以手写：

```json
{
  "type": "object",
  "properties": {
    "path": {"type": "string", "description": "File path to read"},
    "offset": {"type": "integer", "default": 0},
    "limit": {"type": "integer", "default": 200}
  },
  "required": ["path"]
}
```

没有 schema，模型就不知道应该传什么参数。

## Streaming 是什么

模型一次回答可能很长。如果等完整回答结束再显示，用户体验很差。Streaming 是边生成边返回。

常见传输形式是 HTTP chunk 或 Server-Sent Events。你会收到很多小片段：

```text
delta: "我"
delta: "会"
delta: "先"
delta: "检查"
...
```

工具调用也可能在 streaming 中分片返回，尤其是 OpenAI-compatible function calling，arguments 可能一点点拼出来。C++ 实现 provider 时要能累积这些片段。

## Event Loop 是什么

Agent 程序同时要做很多事：

- 等模型网络响应。
- 等子进程输出。
- 等用户在权限弹窗里选择。
- 等 MCP server 返回 JSON-RPC response。
- 等 UI 前端发下一条 JSON line。

event loop 就是专门负责“谁有新事件就处理谁”的机制。Python 用 `asyncio`。C++20 可以用：

- 简单版：线程 + 阻塞队列。
- 进阶版：standalone `asio`。
- 高级版：C++20 coroutine + Asio awaitable。

初版建议不要一开始使用复杂 coroutine。可以先用线程把网络和子进程包起来，统一往 `BlockingQueue<StreamEvent>` 里投递事件。

## JSON Lines 协议

OpenHarness React TUI 和 Python 后端用 stdin/stdout 通信。每条消息是一行 JSON：

```text
{"type":"submit_line","line":"hello"}\n
```

后端发给前端时加了前缀：

```text
OHJSON:{"type":"assistant_delta","message":"hi"}\n
```

这种协议简单、易调试、不需要开放本地端口，适合 CLI/TUI。

## JSON-RPC 和 MCP

MCP 是 Model Context Protocol，用来接入外部工具服务器。它底层类似 JSON-RPC：

```json
{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}
```

server 返回：

```json
{"jsonrpc":"2.0","id":1,"result":{"tools":[...]}}
```

关键点：

- 每个 request 有 id。
- response 用相同 id 对应回去。
- 可能通过 stdio，也可能通过 HTTP stream。
- C++ 里要维护 `id -> promise/callback` 的 pending map。

## 子进程和管道

`bash` 工具、MCP stdio server、后台 task 都需要启动子进程。子进程通常有三条流：

- stdin：写给子进程。
- stdout：子进程标准输出。
- stderr：子进程错误输出。

C++ 里跨平台进程管理比 Python 麻烦：

- Windows 用 `CreateProcessW` 和 pipe。
- Linux/macOS 用 `fork/exec`、pipe 或 `posix_spawn`。
- 如果引入外部进程库，也通过 xmake 管理；但第一版建议自写薄封装，统一隐藏 Windows/POSIX 差异。

## 权限为什么是核心功能

Agent 会被模型驱动，模型可能要求执行：

```text
rm -rf /
cat ~/.ssh/id_rsa
curl secret to remote server
```

所以工具执行前必须有安全判断。OpenHarness 有三层：

1. 敏感路径硬拒绝，例如 `.ssh`、`.aws/credentials`、`.kube/config`。
2. 模式控制，例如 default、plan、full_auto。
3. 用户确认，例如写文件和执行命令前弹窗。

注意：`full_auto` 也不能绕过敏感路径硬拒绝。

## Tool Output 为什么要截断

模型上下文窗口有限。一个命令可能输出几 MB，如果全部塞回模型，会导致：

- 请求太大。
- 费用增加。
- 重要上下文被挤掉。
- provider 直接报错。

所以需要：

- 小输出直接 inline。
- 大输出保存到 artifact 文件。
- 只把 preview 回给模型。
- 后续如果模型需要，再让它读取 artifact。

## C++20 初版建议心法

- 先同步，后异步。
- 先非交互，后 TUI。
- 先一个 provider，后多个 provider。
- 先 3 个工具，后 40 个工具。
- 先 JSON 文件持久化，后 SQLite 或向量搜索。
- 先正确处理错误，后优化性能。

如果你能跑通下面流程，就已经完成了最核心的 agent harness：

```text
用户 prompt
  -> OpenAI-compatible streaming
  -> 模型请求 read_file
  -> C++ 工具读取文件
  -> tool_result 回给模型
  -> 模型给最终回答
```
