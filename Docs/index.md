---
layout: home
hero:
  name: CodeHarness
  text: C++20 coding-agent harness
  actions:
    - theme: brand
      text: 开始使用
      link: guides/getting-started
    - theme: alt
      text: 命令参考
      link: reference/codeharness-command
---

# CodeHarness 中文文档

CodeHarness 是一个用 C++20 编写的本地 Agent harness。它提供模型供应商接入、统一消息模型、工具调用、权限审批、MCP、Skills、Hooks、会话存储、后台任务和终端 TUI。

这组文档按用户视角组织：

- [开始使用](guides/getting-started.md)：构建、启动和第一次运行。
- [配置文件](configuration/config-files.md)：`settings.json`、模型 profile、权限、MCP 和 Hooks。
- [内置工具](reference/tools.md)：Agent 可以调用的工具列表和权限行为。
- [斜杠命令](reference/slash-commands.md)：TUI 和非交互模式支持的控制命令。
- [后续计划](plan/kimi-style-command-parity.md)：当前尚未实现但可参考同类 CLI 补齐的功能。
