# Plugins C++20 实现参考

Plugins 模块的 C++20 实现已完成，代码见 `src/codeharness/plugins/plugin_loader.h/.cpp`。

## 已实现的能力

| 能力 | 说明 |
| --- | --- |
| `PluginManifest` 解析 | 读取 `plugin.json` — name、version、description、skillsDir、hooksFile、mcpFile、commands、agents、hooks |
| `LoadedPlugin` 结构 | 将插件内容解析为签名结构体，包含 manifest、skills、commands、agents、hooks、mcpServers |
| 多来源发现 | `load_plugins()` 从 user plugins (`~/.codeharness/plugins`、`~/.openharness/plugins`) 和 project plugins (`<cwd>/.codeharness/plugins` 等) 加载 |
| 用户/项目插件分离 | `default_user_plugin_roots()` 和 `default_project_plugin_roots()` 分别提供来源 |
| 安全默认值 | project plugins 默认关闭，需 `allow_project_plugins=true` |

## 设计要点

- 兼容 Claude plugin 格式：`.claude-plugin/plugin.json` 也可识别
- 动态 `.dll/.so` 工具不在当前范围——外部工具能力通过 MCP 接入
- Plugin manifest 路径限制在 plugin root 内，防止 `..` 逃逸
- 同名 plugin 后加载不会被静默覆盖（加载器返回所有发现结果，runtime 决定如何合并）

## 加载流程

1. 收集 plugin roots（user → project 顺序）
2. 在每层查找 `plugin.json` 或 `.claude-plugin/plugin.json`
3. 解析 manifest → 判断是否 enabled
4. 如果 enabled，读取 skills、commands、agents、hooks、mcp 配置
5. 返回 `std::vector<LoadedPlugin>`

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- Plugin skills 接入全局 `SkillRegistry`
- Plugin commands 注册到 `CommandRegistry`
- Plugin hooks 接入全局 `HookRegistry`
- `/plugin list` / `/plugin enable|disable` 命令
