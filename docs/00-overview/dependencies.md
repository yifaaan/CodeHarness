# 外部依赖选型

目标是尽量复用成熟库，避免在 C++20 重写 OpenHarness 时重复实现底层能力。

外部库优先从 awesome-cpp 收录项目中挑选，再用 `xmake require --info <package>` 确认可导入。`expected-lite` 是用户确认可导入的例外，用于 C++20 下补足 `std::expected` 能力。当前 `xmake.lua` 已把默认推荐依赖加入 `runtime_packages`。

## 默认导入依赖

| 库 | awesome-cpp 名称 | 用途 | 选择原因 |
| --- | --- | --- | --- |
| `ada` | ada | URL parser | WHATWG URL 解析，适合 provider base URL、web fetch、MCP HTTP |
| `asio` | Asio | 网络 I/O、timer、异步模型 | 满足网络库统一使用 Asio 的约束 |
| `openssl` | OpenSSL | HTTPS/TLS | 与 `asio::ssl` 配合实现 provider/MCP HTTPS |
| `zlib` | ZLib | gzip/deflate | HTTP response 解压 |
| `brotli` | Brotli | br 解压 | 现代 HTTP 常见压缩格式 |
| `nlohmann_json` | json / JSON for Modern C++ | JSON 模型和协议 | 上手快，适合早期开发和调试 |
| `yaml-cpp` | yaml-cpp | YAML frontmatter | skills、memory、agent/plugin markdown frontmatter |
| `re2` | RE2 | grep/search 正则 | C++ API，线性时间匹配，xmake 可直接导入 |
| `cli11` | CLI11 | CLI 参数解析 | 避免自写复杂命令行解析 |
| `expected-lite` | 用户确认例外 | `expected`/Result 类型 | C++20 没有 `std::expected`，用它统一错误返回，后续可迁移到 C++23 `std::expected` |
| `spdlog` | spdlog | 日志 | 成熟、高性能、跨平台 |
| `fmt` | {fmt} | 字符串格式化 | `spdlog` 生态兼容，避免 iostream 拼接复杂字符串 |
| `stduuid` | stduuid | UUID | session id、tool use id、task id、memory id；xmake 使用 `span = true` 走 C++20 `std::span`，避免额外 GSL 依赖 |
| `reproc` | reproc | 子进程管理 | awesome-cpp 收录的跨平台 process 库，适合作为 `ProcessRunner` 底层 |
| `sqlite3` | SQLite | 结构化本地存储 | 后续 session/task/memory 索引可用，先不替代 Markdown memory |
| `doctest` | doctest | 单元测试 | 轻量，已加 `codeharness_tests` 目标 |

## 暂不默认导入

| 库 | 结论 | 原因 |
| --- | --- | --- |
| `llhttp` | 不导入 | 当前未在 awesome-cpp 命中，HTTP framing 和 SSE parser 先放在项目内实现 |
| `tiny-process-library` | 不导入 | 当前未在 awesome-cpp 命中，已改用 awesome-cpp 收录的 `reproc` |
| `cpp-httplib` | 暂不默认 | awesome-cpp 收录，但它会形成第二套网络栈；当前约束是网络 I/O 统一走 standalone Asio |
| `Boost.Beast` | 暂不默认 | awesome-cpp 收录，但会引入 Boost/Boost.Asio；当前先保持 standalone Asio 基础设施 |
| `pcre2` | 暂不默认 | 功能完整，但直接使用是 C API；当前 grep 先选 RE2 的 C++ API |
| `websocketpp` | 暂不默认 | 会引入 Boost，MCP WebSocket 当前优先级低 |
| `libgit2` | 暂不默认 | GPL-2.0-only，许可证和依赖链需要单独评估 |
| `tomlplusplus` | 不导入 | 当前 xmake 检查显示 missing，且配置先用 JSON/YAML 即可 |
| `nlohmann_json_schema_validator` | 不导入 | 当前 xmake 检查显示 missing，先手写关键 schema 校验 |
| `catch2` | 不导入 | `doctest` 更轻，足够当前单元测试 |

## 模块对应关系

| 模块 | 依赖 |
| --- | --- |
| provider/network | `asio`、`openssl`、`ada`、`zlib`、`brotli` |
| MCP | `asio`、`openssl`、`nlohmann_json` |
| tools/search | `re2`、`std::filesystem` |
| tools/bash/tasks | `reproc` |
| config/skills/memory | `nlohmann_json`、`yaml-cpp`、`sqlite3` |
| IDs/session/task | `stduuid` |
| CLI | `cli11` |
| logging/format | `spdlog`、`fmt` |
| error handling | `expected-lite` |
| tests | `doctest` |

## 实现原则

- 外部依赖优先在 awesome-cpp 命中，再确认 xmake 可导入；`expected-lite` 是用户确认可导入的例外。
- 网络基础设施只基于 standalone Asio，不混用 libcurl、cpp-httplib 或 Beast 作为第二套网络栈。
- HTTP framing 和 SSE parser 先放在项目内实现，URL 解析交给 `ada`。
- 搜索正则交给 `re2`，文件遍历用 C++20 `std::filesystem`。
- 子进程先用 `reproc`，只有在无法满足 timeout/pipe/kill 需求时再补平台代码。
- 本地结构化索引用 `sqlite3`，但用户可编辑的 memory/skills/plugins 仍保留 Markdown/JSON/YAML 文件格式。
