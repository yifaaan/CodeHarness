# codeharness 命令

`codeharness` 是当前项目的 CLI 入口。它负责解析参数、加载配置、创建 RuntimeBundle，并根据运行模式进入 TUI、非交互 prompt 或 backend-only 协议。

## 主命令选项

| 选项 | 说明 |
| --- | --- |
| `--version` | 打印版本并退出 |
| `--backend-only` | 启动 JSON Lines backend-only 协议 |
| `--plan` | 以 Plan 权限模式启动 |
| `-p, --prompt <text>` | 非交互执行一条 prompt |
| `--cwd <path>` | 切换工作目录 |
| `--max-turns <n>` | 设置最大 agent loop 轮数 |
| `--provider <type>` | 设置 provider：`openai`、`anthropic`、`echo` |
| `--model <name>` | 设置模型名 |
| `--api-key <key>` | 当前进程直接提供 API key |
| `--base-url <url>` | 设置 API base URL |
| `--profile <id>` | 使用指定配置 profile |

## 典型用法

启动 TUI：

```powershell
codeharness
```

执行单条 prompt：

```powershell
codeharness -p "解释 src/codeharness/runtime 的启动流程"
```

指定工作目录：

```powershell
codeharness --cwd D:\code\CodeHarness -p "列出可运行的测试"
```

本地 echo 测试：

```powershell
codeharness --provider echo -p "hello"
```

Backend-only：

```powershell
codeharness --backend-only
```

## 斜杠命令路径

当 `--prompt` 的内容以 `/` 开头时，CLI 会先执行斜杠命令。消息型命令直接输出；Skill/Plugin 命令可以渲染成 prompt 后继续进入模型循环。
