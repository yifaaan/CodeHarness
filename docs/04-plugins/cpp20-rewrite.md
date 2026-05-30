# Plugins C++20 重写方案

Plugins 是扩展包。一个插件可以贡献 skills、commands、agents、hooks、MCP server 配置，甚至 Python 版还支持动态工具。

上游关键文件：

- `docs/OpenHarness/src/openharness/plugins/schemas.py`
- `docs/OpenHarness/src/openharness/plugins/types.py`
- `docs/OpenHarness/src/openharness/plugins/loader.py`
- `docs/OpenHarness/src/openharness/plugins/installer.py`

## 插件目录布局

常见布局：

```text
plugins/<plugin-name>/
  plugin.json
  skills/<skill>/SKILL.md
  commands/*.md
  agents/*.md
  hooks.json
  mcp.json
```

兼容 Claude plugin 时，也可能是：

```text
.claude-plugin/plugin.json
```

## PluginManifest

```cpp
struct PluginManifest {
    std::string name;
    std::string version = "0.0.0";
    std::string description;
    bool enabledByDefault = true;
    std::string skillsDir = "skills";
    std::string toolsDir = "tools";
    std::string hooksFile = "hooks.json";
    std::string mcpFile = "mcp.json";
    nlohmann::json commands;
    nlohmann::json agents;
    nlohmann::json hooks;
};
```

## LoadedPlugin

```cpp
struct LoadedPlugin {
    PluginManifest manifest;
    std::filesystem::path path;
    bool enabled = false;
    std::vector<SkillDefinition> skills;
    std::vector<PluginCommandDefinition> commands;
    std::vector<AgentDefinition> agents;
    std::vector<HookDefinition> hooks;
    std::vector<McpServerConfig> mcpServers;
};
```

## 插件来源

建议支持：

- user plugins：`~/.openharness/plugins`
- project plugins：`<cwd>/.openharness/plugins`
- extra plugin roots：例如 ohmo workspace

安全默认值：

- user plugins 可以默认启用。
- project plugins 默认关闭，需要 `allow_project_plugins=true`。

原因：项目仓库里的 plugin 可能由别人提交，不能默认执行 hooks 或命令。

## C++ 第一版不要做 native dynamic tools

Python 版可以动态 import `<plugin>/tools/*.py`。C++ 版不建议第一版做 `.dll/.so` 插件工具，因为：

- ABI 不稳定。
- 崩溃会带崩主进程。
- 权限和沙箱复杂。
- Windows/macOS/Linux 差异大。

推荐第一版只支持：

1. plugin skills。
2. plugin commands。
3. plugin agents。
4. plugin hooks。
5. plugin MCP servers。

外部工具能力通过 MCP 接入，而不是 native 动态库。

## PluginLoader

```cpp
class PluginLoader {
public:
    PluginLoader(Settings settings);
    std::vector<LoadedPlugin> load(const std::filesystem::path& cwd);
};
```

加载流程：

1. 收集 plugin roots。
2. 查找 `plugin.json` 或 `.claude-plugin/plugin.json`。
3. 解析 manifest。
4. 判断是否 enabled。
5. 如果 enabled，加载 skills、commands、agents、hooks、mcp。
6. 返回 `LoadedPlugin` 列表。

## Plugin commands

命名建议保留 namespace：

```text
/pluginName:command
/pluginName:namespace:command
```

这样避免不同插件命令冲突。

Plugin command 本质是 Markdown prompt：

- `disable_model_invocation=false`：提交给模型。
- `disable_model_invocation=true`：直接显示内容。

## Plugin hooks

插件 hooks 应合并进全局 `HookRegistry`，但 UI 或日志要能显示来源。

```cpp
struct HookDefinition {
    std::string sourcePlugin;
    ...
};
```

## Plugin MCP

插件可以贡献 MCP server 配置。加载后合并到 `McpClientManager`。

冲突策略：

- 同名 server 后加载覆盖前加载，或拒绝并报警。
- 建议第一版拒绝重复名字，避免不可预测。

## 安全策略

- Project plugin 默认关闭。
- Plugin manifest 路径必须在 plugin root 内。
- commands/skills/agents 路径不能 `..` 逃逸。
- hooks command 必须有 timeout。
- MCP server 启动失败不应崩溃。
- 禁用插件时，不加载它的 hooks/MCP/commands。

## 第一版路线

1. 解析 `plugin.json`。
2. 加载 user plugin skills。
3. 加载 plugin commands。
4. 加载 plugin hooks。
5. 加载 plugin MCP config。
6. 实现 `/plugin list`。
7. 实现 enable/disable 配置。

## 测试清单

- disabled plugin 不贡献任何扩展。
- project plugin 默认不加载。
- enabled plugin skill 可在 registry 查到。
- command namespace 正确。
- hooks priority 合并正确。
- 重复 plugin name 行为明确。
