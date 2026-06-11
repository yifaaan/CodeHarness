# 设置、主题与外部编辑器计划

## 目标

补齐 TUI 中的设置面板、主题切换和外部编辑器入口，让常用运行时偏好不必手写配置文件。

## 当前缺口

- 当前有 TUI 主题渲染代码，但没有用户级 `/theme` 命令和主题配置持久化。
- 没有 `/settings` 或 `/config` 设置面板。
- 没有 `/editor` 命令，也没有 `Ctrl-G` 外部编辑器工作流。
- 快捷键文档只能记录少量稳定行为。

## 建议落点

- `src/codeharness/tui/`：设置面板、主题列表、编辑器弹窗。
- `src/codeharness/config/config.*`：持久化 UI 偏好。
- `src/codeharness/commands/command_registry.*`：注册 `/settings`、`/theme`、`/editor`。

## 验收标准

- TUI 可切换主题并持久化。
- 用户可配置外部编辑器命令。
- 设置面板能展示当前 provider、model、permission mode 和路径。
- 快捷键帮助可从实际绑定生成或集中维护。
