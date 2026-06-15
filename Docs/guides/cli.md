# CLI 快速开始

CodeHarness 现在有一个非交互式可执行程序 `codeharness_cli`：解析命令行参数、加载 `config.toml`、解析 provider、创建 Session、运行一次 prompt，并把助手的文本流式输出到 stdout。本文档是 MVP（`--prompt` 一次性模式）的速查；交互式 TUI/REPL 属于后续工作。

## 构建

```bash
cmake --preset windows-msvc
cmake --build build/cmake/windows-msvc --config Debug
```

默认构建会同时产出 `codeharness_cli` 和 `codeharness_tests`。可执行文件位于：

```text
build/cmake/windows-msvc/Source/codeharness/Debug/codeharness_cli(.exe)
```

## 前置：配置 config.toml

CLI 启动时会通过 `ConfigManager` 读取配置。配置路径解析顺序与其它模块一致：

1. `$CODEHARNESS_HOME/config.toml`（若设置了 `CODEHARNESS_HOME`）
2. `$HOME/.codeharness/config.toml`

最少需要的配置：一个 `[providers]` 条目（含 `api_key`、`base_url`）和一个指向它的 `[models]` 条目，再加 `default_model`。完整字段说明见 [配置文件](../configuration/config-files.md) 与 [Provider 配置](../configuration/providers.md)。

最小示例：

```toml
default_model = "gpt-4o"

[providers.openai]
type = "openai"
base_url = "https://api.openai.com/v1"
api_key = "$OPENAI_API_KEY"   # 支持 $VAR / ${VAR} 展开

[models."gpt-4o"]
provider = "openai"
model = "gpt-4o"
```

## 命令行参数

```text
codeharness_cli [OPTIONS]

OPTIONS:
  -h,     --help              打印帮助并退出
  -p,     --prompt TEXT       要发送给模型的一次性 prompt（必需）
  -m,     --model TEXT        模型别名；默认取 config 的 default_model
          --workdir TEXT      工作目录；默认为当前目录
  -y,     --yolo              不提示直接允许所有工具动作（Yolo 权限模式）
  -V,     --version           打印版本并退出
```

## 运行一次 prompt

```bash
# 用默认模型
codeharness_cli -p "用一句话解释什么是事件溯源"

# 指定模型与工作目录，并启用 yolo（允许读写文件 / 执行命令而不逐次确认）
codeharness_cli -p "把这个目录里的 TODO 整理成列表" -m gpt-4o --workdir . -y
```

输出会流式打印到 **stdout**（每个 `AssistantDeltaEvent` 即时 flush），权限拒绝、错误信息打到 **stderr**。退出码：`0` 成功；`1` 参数错误；`2` 运行期错误（配置缺失、provider 解析失败、prompt 失败等）。

## 权限模式

- 默认（不加 `-y`）：`Manual` 模式。当模型请求会改变外部状态的工具（`Write`/`Edit`/`Bash`）时，CLI 会在 stderr 打印工具名 + 描述 + 参数，并从 stdin 读一行 `y/N`。读到 EOF 或非 `y` 一律视为拒绝。
- `-y/--yolo`：`Yolo` 模式，所有工具直接放行，不交互。**仅在你信任 prompt 与工作目录时使用。**

只读工具（`Read`/`Glob`/`Grep`）在任何模式下都不需要确认。

## 日志

默认 spdlog 级别为 `warn`。可用环境变量提升：

```bash
# Windows (cmd)
set CODEHARNESS_LOG_LEVEL=debug
# 或
set SPDLOG_LEVEL=debug
```

## 会话持久化

每次运行都会在 sessions 根目录下创建一个新会话，prompt + 助手回复 + 工具事件会追加到该会话的 `wire.jsonl`。sessions 根目录解析见 [会话与上下文](sessions.md)。MVP 阶段 CLI 不支持 `--continue` / `--session` 恢复已存在的会话（Session 模块已支持 `Resume`，只是 CLI 标志尚未接入）。

## 当前限制（后续工作）

- 仅 `--prompt` 一次性模式；无 REPL / 多轮。
- 无 TUI / `shell` 模式、无 reverse-RPC 审批面板。
- 输出格式仅纯文本；`--output-format stream-json` 待实现。
- 无配置自动生成向导；首次运行若缺配置会返回明确错误。
