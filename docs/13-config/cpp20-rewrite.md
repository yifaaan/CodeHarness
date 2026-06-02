# Config C++20 重写方案

Config 模块负责读取和合并 settings、provider profile、路径、权限、MCP、sandbox、memory 等配置。

上游关键文件：

- `docs/OpenHarness/src/openharness/config/settings.py`
- `docs/OpenHarness/src/openharness/config/paths.py`
- `docs/OpenHarness/src/openharness/config/schema.py`
- `docs/OpenHarness/src/openharness/auth/*`

## 配置来源

建议合并顺序：

```text
defaults
  -> config file
  -> environment variables
  -> CLI flags
```

越靠后优先级越高。

## 路径

默认路径建议：

```text
~/.codeharness/
  settings.json
  credentials.json
  skills/
  plugins/
  agents/
  themes/
  output_styles/
  data/
    sessions/
    memory/
    tasks/
    tool_artifacts/
```

如果想兼容上游，也可以继续读 `~/.openharness`，但 C++ 项目建议有自己的 `~/.codeharness`。

## Settings 结构

```cpp
struct Settings {
    std::string activeProfile = "default";
    std::map<std::string, ProviderProfile> profiles;

    std::string model;
    std::string apiFormat; // openai | anthropic
    std::string baseUrl;
    int maxTokens = 4096;
    int maxTurns = 200;

    PermissionSettings permission;
    std::vector<McpServerConfig> mcpServers;
    MemorySettings memory;
    SandboxSettings sandbox;

    bool allowProjectSkills = true;
    bool allowProjectPlugins = false;
};
```

## ProviderProfile

```cpp
struct ProviderProfile {
    std::string name;
    std::string label;
    std::string apiFormat;
    std::string model;
    std::string baseUrl;
    std::string authSource;
    std::map<std::string, std::string> extraHeaders;
};
```

第一版只需要：

- OpenAI-compatible profile。
- Anthropic-compatible profile。
- API key from env 或 credentials file。

## Credentials

不要把 API key 放进项目仓库。建议：

```text
~/.codeharness/credentials.json
```

结构：

```json
{
  "profiles": {
    "default": {
      "api_key": "..."
    }
  }
}
```

安全要求：

- 日志隐藏 key。
- 文件权限尽量限制。
- 不通过 prompt 注入 key。

## ConfigLoader

```cpp
class ConfigLoader {
public:
    Settings load(const CliOptions& cli) const;

private:
    Settings loadDefaults() const;
    Settings loadFile(std::filesystem::path path) const;
    void applyEnv(Settings& settings) const;
    void applyCli(Settings& settings, const CliOptions& cli) const;
};
```

## JSON 解析

使用 `nlohmann-json` 或 `glaze`。第一版推荐 `nlohmann-json`，因为调试和错误信息更直观。

需要为每个配置结构提供：

- `from_json`
- `to_json`
- 默认值填充
- enum parse

## vcpkg 依赖

配置模块推荐依赖：

```json
{
  "dependencies": [
    "nlohmann-json",
    "yaml-cpp",
    "sqlite3"
  ]
}
```

`yaml-cpp` 用于 skills/memory/agents 的 frontmatter，`sqlite3` 用于后续 session/task/memory 的结构化索引。用户可编辑内容仍建议保留 Markdown/JSON/YAML 文件。

## Project config

项目级配置可以放：

```text
<project>/.codeharness/settings.json
```

安全默认值：

- project skills 可开启。
- project plugins 默认关闭。
- project hooks 默认不要自动执行，除非用户显式允许。

## Dry run

上游有 dry-run，可以预览配置是否 ready。C++ 后续也应支持：

```text
codeharness --dry-run -p "explain this repo"
```

输出：

- 当前 profile。
- auth 是否 ready。
- model/base_url。
- tools 数量。
- skills 数量。
- MCP server 状态。
- readiness：ready / warning / blocked。

## 测试清单

- 默认配置可加载。
- settings.json 覆盖默认值。
- env 覆盖 settings。
- CLI 覆盖 env。
- invalid enum 有清晰错误。
- credentials 不被日志输出。
- project plugins 默认关闭。
