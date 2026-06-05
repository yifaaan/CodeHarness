# permissions/ — 权限系统模块

## 设计目标

在工具执行前进行权限检查，防止危险操作（如敏感文件读写、高危命令执行）。这是 CodeHarness 安全体系的核心。

## 架构

```
PermissionChecker::evaluate(tool_name, is_read_only, path, command)
  └─ PermissionDecision { Allow | Ask | Deny }
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `PermissionMode` | 三种模式：`Default`（只读自动允许，写操作需确认）、`Plan`（只读模式）、`FullAuto`（完全自动，但敏感路径仍拒绝） |
| `PermissionChecker` | 权限评估核心：检查工具名称、只读标志、目标路径、命令字符串 |
| `PermissionDecision` | 决策结果：Allow / Ask（需用户确认）/ Deny |

## 设计要点

- **敏感路径硬拒绝**：即在 `FullAuto` 模式下，涉及敏感路径的操作仍然被拒绝，不可绕过
- 只读工具（如 `read_file`、`glob`、`grep`）在 Default 模式下自动允许
- 权限评估在工具执行之前调用，属于 `PreToolUse` 阶段的一部分

## 初学者指南

- 如果你想理解安全边界，从这个模块开始
- 核心原则：写操作需要确认，敏感路径永远不允许
- 路径是在 `tools/` 模块的 `PermissionTarget` 中从工具输入参数提取出来的
- 权限决策流程：`Engine` → `HookExecutor(PreToolUse)` → `PermissionChecker::evaluate()` → 决策
