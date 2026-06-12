# 常见问题

## CodeHarness 当前能做什么？

CodeHarness 当前已经具备基础 coding-agent 工作流：读取和编辑文件、运行 shell、搜索文件、调用网页抓取/搜索工具、执行后台任务、加载 Skills、连接 MCP server，并通过权限系统控制写入和执行类操作。

## 它和普通聊天 CLI 有什么区别？

CodeHarness 的核心是 agent loop：模型可以返回文本，也可以请求工具调用；工具结果会回填为下一轮模型输入，直到任务完成或达到最大轮数。CLI、TUI、backend-only 协议都围绕同一套 runtime 和 engine 运行。

## 默认配置文件在哪里？

默认配置目录是 `~/.codeharness`，Windows 上通常是 `%USERPROFILE%\.codeharness`。配置文件是 `settings.json`，凭据文件是 `credentials.json`，会话、任务、记忆等运行数据默认放在 `data/` 子目录下。

## 没有 API key 可以运行吗？

可以使用 `--provider echo` 做本地回显测试。真实模型调用需要为 `openai` 或 `anthropic` 提供 API key，可以通过环境变量、CLI 参数、`settings.json` profile 或 `credentials.json` 配置。

## 当前支持哪些供应商？

当前运行时代码支持 `openai`、`anthropic` 和 `echo`。其它供应商应先进入计划或源码实现，不应只在文档中声明已支持。

## 为什么有些同类 CLI 常见功能在这里没有？

CodeHarness 正处在 C++20 重写和交互体验补齐阶段。当前源码没有实现的登录、主题、外部编辑器、上下文压缩、会话 fork/export、多媒体读取等功能，统一记录在 [后续计划](plan/kimi-style-command-parity.md) 中。
