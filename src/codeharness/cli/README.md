# cli/ — CLI 入口模块

## 设计目标

`cli/` 是 CodeHarness 的顶层可执行入口。它只负责一件事：将命令行参数转化为 `RuntimeBundle::run_prompt()` 调用。

## 架构

```
main()
  └─ run_cli(argc, argv)          ← cli.h
       ├─ CLI11 解析参数
       ├─ create_runtime_bundle()   ← runtime/ 模块
       └─ runtime.run_prompt(...)   ← 进入引擎循环
```

### 关键文件

| 文件 | 职责 |
|------|------|
| `cli.h`   | 声明 `run_cli(argc, argv)`，返回 `Result<int>` |
| `cli.cpp` | 实现：CLI11 参数绑定 → 构造 RuntimeBundle → 执行 prompt |

## 核心流程

1. 用 CLI11 解析命令行参数（`-p` prompt、`--model`、`--max-turns` 等）
2. 调用 `create_runtime_bundle()` 组装所有子系统
3. 通过 `build_run_request()` 构造 `RunRequest`
4. 调用 `runtime.run_prompt(request)` 进入 `Engine::run()` 阻塞式 agent 循环
5. 将 `RunResult` 输出到 stdout
