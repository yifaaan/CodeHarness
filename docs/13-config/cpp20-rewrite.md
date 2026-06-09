# Config C++20 实现参考

Config 模块的 C++20 实现已完成，代码见 `src/codeharness/config/`（9 个文件）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| `ConfigLoader` | `config/config_loader.h/.cpp` — 4 层合并（defaults → settings.json → env → CLI） |
| `Settings` 结构 | `config/settings.h` — activeProfile、profiles、model、apiFormat、baseUrl、maxTokens、maxTurns、permission、mcpServers、memory、sandbox、allowProjectSkills、allowProjectPlugins |
| `ProviderProfile` | `config/settings.h` — name、label、apiFormat、model、baseUrl、authSource、extraHeaders |
| `Credentials` | `config/credentials.h/.cpp` — `~/.codeharness/credentials.json`，profiles 内嵌 api_key |
| 多来源合并 | 默认值 → `~/.codeharness/settings.json` → 环境变量 → CLI flags（`cli11`） |
| JSON 解析 | 基于 `nlohmann-json`，每结构提供 `from_json`/`to_json`、默认值、enum 解析 |
| 路径发现 | `config/paths.h/.cpp` — `default_paths()` → 返回 `~/.codeharness` 下各子目录 |
| API Key 解析 | 支持环境变量（`OPENAI_API_KEY`、`ANTHROPIC_API_KEY`、`COHERE_API_KEY` 等）和 credentials.json |
| 兼容上游 | 同时读 `~/.openharness` 下的配置和 skills/plugins/agents |

## 默认路径结构

```
~/.codeharness/
  settings.json        主配置
  credentials.json     认证信息（API keys）
  skills/              user skills
  plugins/             user plugins
  agents/              user agent definitions
  themes/              主题
  output_styles/       输出格式
  data/
    sessions/          JSON session 快照
    memory/            Markdown memory 文件
    tasks/             后台任务状态和日志
    teams/             team.json 持久化
    tool_artifacts/    工具输出 artifact
```

## 合并优先级

```
Defaults → ~/.codeharness/settings.json → 环境变量 → CLI flags
                              (越高优先级)
```

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- **Dry run**：`codeharness --dry-run` 预览配置 readiness
- **SQLite** 结构化索引（当前使用 JSON/Markdown 文件）
- **Sandbox settings**（sandbox 字段已预留但未使用）
