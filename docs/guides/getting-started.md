# 开始使用

## CodeHarness 是什么

CodeHarness 是一个运行在终端中的 C++20 coding-agent harness。它可以接入 OpenAI-compatible 或 Anthropic 模型，让 Agent 读取代码、编辑文件、执行命令、调用 MCP 工具，并通过权限系统控制风险。

## 构建

Windows 当前推荐 MSVC preset：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Linux debug preset：

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## 第一次启动

进入项目目录后运行：

```powershell
codeharness
```

执行一条非交互 prompt：

```powershell
codeharness -p "帮我看一下这个项目的目录结构"
```

使用 echo provider 做本地 smoke test：

```powershell
codeharness --provider echo -p "hello"
```

## 配置模型

最小 OpenAI-compatible 示例：

```powershell
$env:OPENAI_API_KEY = "sk-..."
codeharness --provider openai --model gpt-4.1 -p "总结当前目录"
```

也可以写入 `~/.codeharness/settings.json`，详见 [配置文件](../configuration/config-files.md)。

## 权限模式

默认模式下，只读工具通常自动放行，写入和命令执行需要确认。启动时加 `--plan` 会进入只读分析模式：

```powershell
codeharness --plan
```

TUI 内也可以通过 `/plan`、`/act`、`/fullauto` 和 `/default` 切换模式。
