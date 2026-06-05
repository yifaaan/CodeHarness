# plugins/ — 插件加载模块

## 设计目标

支持通过插件扩展 CodeHarness 功能。插件可以携带技能（skills）、斜杠命令（commands）和 MCP 服务器配置。

## 架构

```
PluginManifest
  ├─ name / version / description
  ├─ skills[]    ← 提供的技能定义路径
  ├─ commands[]  ← 提供的命令定义路径
  └─ mcp_servers[]  ← MCP 服务器配置

LoadedPlugin = PluginManifest + 解析后的 skills + commands + MCP configs

扫描优先级：
  1. 默认用户目录
  2. 用户指定的额外目录
  3. 项目目录
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `PluginManifest` | 插件清单（YAML 格式），声明插件包含的技能、命令、MCP 配置 |
| `LoadedPlugin` | 加载后的插件，包含清单和解析后的运行时对象 |
| `load_plugin_manifests()` | 扫描文件系统并加载插件 |

## 设计要点

- 插件是文件和配置的集合，而非动态链接库
- 优先级覆盖：用户目录 > 额外目录 > 项目目录，后加载的可以覆盖先加载的
- 插件加载是初始化阶段的一次性操作

## 初学者指南

- 插件的本质是"打包好的技能集+工具配置"
- 如果你想创建插件，只需要创建符合 `PluginManifest` 格式的 YAML 文件并放入插件目录
- 加载流程：`create_runtime_bundle()` → `load_skill_registry_with_plugins()` → 扫描目录 → 解析 manifest → 注册技能/命令/MCP
