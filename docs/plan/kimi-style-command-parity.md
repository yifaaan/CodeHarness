# 命令体验补齐计划

## 目标

在保持 CodeHarness 架构的前提下，补齐现代 coding-agent CLI 常见命令体验，包括账号、设置、状态、模型、会话和帮助。

## 当前已支持

- `/skills`
- `/plan`
- `/act`
- `/fullauto`
- `/full_auto`
- `/default`
- `/mode`
- `/memory`
- `/sessions`
- `/resume`
- `/plugin`
- 动态 Skill/Plugin 命令

## 当前缺口

- `/help`
- `/status`
- `/version`
- `/model`
- `/settings`
- `/permission`
- `/theme`
- `/editor`
- `/new`
- `/clear`
- `/fork`
- `/title`
- `/compact`
- `/mcp`
- `/exit`

## 建议落点

- `src/codeharness/commands/command_registry.*`：命令注册和帮助输出。
- `src/codeharness/tui/tui_app.*`：TUI 状态命令和弹窗。
- `src/codeharness/runtime/runtime.*`：模型 profile 切换、MCP 状态、权限模式。
- `src/codeharness/ui_backend/`：backend-only 协议对应事件。

## 验收标准

- `/help` 从 registry 生成，不手写重复命令表。
- `/model` 调用 RuntimeBundle 的 profile 切换。
- `/status` 显示版本、cwd、provider、model、permission mode。
- TUI、CLI prompt 路径和 backend-only 行为保持一致。
