# OAuth 与登录命令计划

## 目标

为 CodeHarness 增加用户可见的登录与登出流程，让用户无需手写 API key 文件即可配置常见供应商凭据。

## 当前缺口

- 当前源码通过环境变量、CLI 参数、`settings.json` 和 `credentials.json` 读取 API key。
- 没有 `/login`、`/logout` 命令。
- 没有 OAuth device code 或浏览器授权流程。

## 建议落点

- `src/codeharness/config/credentials.*`：扩展凭据存储结构。
- `src/codeharness/commands/command_registry.*`：注册登录相关 slash commands。
- `src/codeharness/tui/`：提供交互式账号选择和输入 UI。
- `src/codeharness/provider/`：为需要 OAuth 的 provider 增加 token 注入。

## 验收标准

- 用户可在 TUI 中启动登录流程并保存凭据。
- `/logout` 可删除指定 profile 凭据。
- 凭据不写入 `settings.json`。
- 无凭据时仍能使用 `echo` provider。
