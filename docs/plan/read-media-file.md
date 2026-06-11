# 多媒体读取工具计划

## 目标

增加图片和视频读取能力，让支持视觉输入的模型可以分析截图、设计稿和短视频片段。

## 当前缺口

- 当前内置工具没有 `read_media_file`。
- Provider profile 没有模型能力字段来判断 `image_in` 或 `video_in`。
- TUI 没有图片/视频粘贴入口。

## 建议落点

- `src/codeharness/tools/`：新增 `ReadMediaFileTool`。
- `src/codeharness/provider/`：扩展消息序列化，支持多模态内容块。
- `src/codeharness/runtime/`：按模型能力决定是否注册工具。
- `src/codeharness/tui/`：处理粘贴文件或剪贴板图片。

## 验收标准

- 工具只接受工作区内或明确允许的文件路径。
- 大文件有上限和清晰错误。
- 不支持视觉的模型不会暴露该工具。
- 工具结果通过统一消息模型进入 provider。
