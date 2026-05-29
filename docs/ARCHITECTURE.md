# CodeHarness 架构文档

## 概述

CodeHarness 是对 [CodeWhale](https://github.com/Hmbown/CodeWhale)（Rust TUI 编码代理）的 C++23 复刻项目。本文档记录项目结构、模块映射、外部依赖及构建方式。

## 项目结构

```
src/
├── main.cpp                          # 入口: codeharness::cli::run()
└── codeharness/
    ├── cli/          cli.hpp/.cpp     # CLI 调度器 (对应 crates/cli)
    ├── tui/          tui.hpp/.cpp     # TUI 交互式代理 (对应 crates/tui)
    ├── app-server/   app_server.hpp/  # HTTP 服务端 (对应 crates/app-server)
    ├── core/         core.hpp/.cpp    # 核心运行时 (对应 crates/core)
    ├── agent/        agent.hpp/.cpp   # 模型注册表 (对应 crates/agent)
    ├── config/       config.hpp/.cpp  # 配置加载/保存 (对应 crates/config)
    ├── protocol/     protocol.hpp     # 通信协议类型 (对应 crates/protocol)
    ├── tools/        tools.hpp/.cpp   # 工具执行框架 (对应 crates/tools)
    ├── execpolicy/   execpolicy.hpp/  # 执行策略引擎 (对应 crates/execpolicy)
    ├── hooks/        hooks.hpp/.cpp   # 事件钩子系统 (对应 crates/hooks)
    ├── mcp/          mcp.hpp/.cpp     # MCP 服务器管理 (对应 crates/mcp)
    ├── secrets/      secrets.hpp/.cpp # API Key 凭据存储 (对应 crates/secrets)
    ├── state/        state.hpp/.cpp   # SQLite 持久化 (对应 crates/state)
    └── tui-core/     tui_core.hpp/    # TUI 核心状态管理 (对应 crates/tui-core)
```

## CodeWhale 到 CodeHarness 映射

| CodeWhale (Rust crate) | CodeHarness (C++ namespace) | 职责 |
|---|---|---|
| `crates/cli` | `codeharness::cli` | CLI 参数解析与命令分发 |
| `crates/tui` | `codeharness::tui` | 主 TUI 交互循环 |
| `crates/app-server` | `codeharness::appserver` | HTTP/SSE 服务端 |
| `crates/core` | `codeharness::core` | 运行时 Orchestrator |
| `crates/agent` | `codeharness::agent` | 模型信息与解析 |
| `crates/config` | `codeharness::config` | 配置读写 |
| `crates/protocol` | `codeharness::protocol` | 请求/响应/事件类型 |
| `crates/tools` | `codeharness::tools` | 工具注册与调用 |
| `crates/execpolicy` | `codeharness::execpolicy` | 命令执行策略 |
| `crates/hooks` | `codeharness::hooks` | 事件钩子分发 |
| `crates/mcp` | `codeharness::mcp` | MCP 服务器生命周期 |
| `crates/secrets` | `codeharness::secrets` | API 密钥存储 |
| `crates/state` | `codeharness::state` | SQLite 状态持久化 |
| `crates/tui-core` | `codeharness::tuicore` | TUI 状态机 |

## 外部依赖映射

| CodeWhale (Rust) | CodeHarness (C++) | 用途 |
|---|---|---|
| `clap` | CLI11 | CLI 参数解析 |
| `serde` + `serde_json` | nlohmann_json | JSON 序列化 |
| `toml` | toml11 | TOML 配置解析 |
| `reqwest` | Asio + Beast | HTTP 客户端 |
| `axum` + `tower-http` | Asio + Beast | HTTP 服务端 |
| `rusqlite` | sqlite3 | SQLite 数据库 |
| `uuid` | stduuid | UUID 生成 |
| `sha2` | OpenSSL | SHA-256 哈希 |
| `chrono` | date | 日期时间 |
| `tracing` / `tracing-subscriber` | spdlog + fmt | 日志 |
| `tokio` | Asio | 异步运行时 |
| `-` | ada | URL 解析 |
| `-` | reproc | 子进程管理 |
| `-` | glob | 文件通配 |
| `-` | doctest | 单元测试 |

## 构建系统

使用 xmake v3.x，C++23 标准。

```bash
# 配置并构建
xmake f
xmake

# 运行
xmake run codeharness

# 测试
xmake run codeharness_tests

# 调试模式
xmake f -m debug
xmake
```

### xmake.lua 关键配置

- `set_languages("c++23")` — C++23 标准
- 所有外部依赖通过 `add_requires` + `add_packages` 管理
- Windows 平台自动链接 `ws2_32`, `crypt32`, `bcrypt`

## 开发约定

1. **命名空间**: 全小写 `codeharness::<module>`
2. **头文件**: `#pragma once`，不使用 `#define` 宏守卫
3. **函数签名**: 使用 trailing return type `auto foo() -> int`
4. **代码风格**: 无 `//` 或 `/* */` 注释
5. **标准**: C++23，优先使用 `std::` 替代外部库（如 `std::expected` 替代 `thiserror`/`anyhow`）

## 实现状态

- [ ] 项目骨架搭建 ✓
- [ ] CLI 调度器 (cli)
- [ ] TUI 交互式代理 (tui)
- [ ] HTTP 服务端 (app-server)
- [ ] 核心运行时 (core)
- [ ] 模型注册表 (agent)
- [ ] 配置系统 (config)
- [ ] 通信协议类型 (protocol)
- [ ] 工具执行框架 (tools)
- [ ] 执行策略 (execpolicy)
- [ ] 事件钩子 (hooks)
- [ ] MCP 管理 (mcp)
- [ ] 凭据存储 (secrets)
- [ ] SQLite 持久化 (state)
- [ ] TUI 状态管理 (tui-core)
