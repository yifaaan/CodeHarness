# core/ — 基础基础设施模块

## 设计目标

提供整个 CodeHarness 共享的基础类型、工具函数和基础设施。所有其他模块都依赖此模块，它不依赖项目中的任何其他模块。

## 内容一览

### 消息模型 (`message.h`)
- `Message`、`ContentBlock`（`TextBlock` | `ToolUseBlock` | `ToolResultBlock`）
- `Role` 枚举（System / User / Assistant / Tool）
- 辅助函数：`make_text_message()`、`make_tool_result_message()`、`collect_text()`、`collect_tool_uses()`

### 错误处理 (`error.h` + `result.h`)
- `CodeHarnessError` 带 `ErrorKind` 枚举（InvalidArgument、Config、Io、Network、Provider、Tool、Internal、AlreadyExists、NotFound）
- `Result<T>` 别名 = `nonstd::expected<T, CodeHarnessError>`
- `fail()` 快速构造错误结果

### JSON 解析 (`json_parse.h`)
- 类型安全 JSON 字段读取模板：`read_json_field<T>()`、`read_optional_json_field<T>()`、`expect_json_field<T>()`
- 支持 string / int / bool / vector / map 等

### 工具函数
| 文件 | 功能 |
|------|------|
| `paths.h` | 路径安全操作：`home_directory()`、`ensure_directory()`、`is_safe_relative_path()` |
| `strings.h` | `trim()`、`next_line()` |
| `time.h` | `utc_timestamp_seconds()` |
| `log.h` | spdlog 日志初始化 |
| `overloaded.h` | `Overloaded` — `std::visit` 模式匹配辅助 |
| `assign.h` | `assign(target, Result<Value>&&)` — 结果解包 |
| `shell.h` | 跨平台 shell 前缀（Windows: cmd.exe, Linux: /bin/sh） |
| `event_collector.h` | `ProviderEventCollector` — 将流式 ProviderEvent 合并为 Message |

## 设计原则

- **值语义优先**：`Message`、`ContentBlock` 等核心类型均使用 `std::variant` 和值类型
- **类型安全**：用 `enum class` 代替布尔标志，用 `Result<T>` 代替异常
- **零外部依赖**（仅依赖标准库和第三方库）：此模块只引入如 `nlohmann-json`、`spdlog`、`expected-lite`、`date` 等外部库