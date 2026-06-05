# runtime/ — 运行时组装模块（Composition Root）

## 设计目标

作为 Composition Root（组合根），将所有子模块装配成可工作的系统。这是依赖注入的集中地，也是从"零散模块"到"完整应用"的最后一公里。

## 架构

```
create_runtime_bundle()
  └─ RuntimeBundle
       ├─ SkillRegistry            ← skills/
       ├─ MemoryStore              ← memory/
       ├─ CommandRegistry          ← commands/
       ├─ ToolRegistry             ← tools/
       ├─ CoordinatorRuntime       ← coordinator/
       ├─ PermissionChecker        ← permissions/
       ├─ EchoProvider             ← provider/
       └─ Engine                   ← engine/

RuntimeBundle::run_prompt(request)
  └─ build_run_request() → Engine::run() → RunResult
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `RuntimeBundle` | 运行时容器，持有所有子系统实例 |
| `create_runtime_bundle()` | 工厂函数，完成所有模块的装配 |
| `build_run_request()` | 将用户输入构造为 `RunRequest`（包括 system prompt 组装） |
| `run_prompt()` | 顶层入口：组装请求 → 执行引擎 → 返回结果 |

## 设计要点

- 单一组装点：所有 `new` 和依赖注入都在 `create_runtime_bundle()` 中发生
- 模块间的耦合在运行时显式连接，而非编译时隐式依赖
- `run_prompt()` 是 CLI 和 UI Backend 的共同入口点

## 初学者指南

- 如果你想理解各个模块如何连接在一起，这是最佳起点
- 阅读 `create_runtime_bundle()` 就能看清整个系统的依赖关系
- 核心路径：用户输入 → `cli/` 或 `ui_backend/` → `RuntimeBundle::run_prompt()` → `Engine::run()` → Provider + Tools
- `AGENTS.md` 中描述的核心执行路径就汇集在这个模块中
