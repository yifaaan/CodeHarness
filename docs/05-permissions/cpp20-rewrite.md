# Permissions C++20 实现参考

Permissions 模块的 C++20 实现已完成，代码见 `src/codeharness/permissions/permission.h/.cpp`（约 360 行）。

## 已实现的能力

| 能力 | 代码/说明 |
| --- | --- |
| 三种模式 | `PermissionMode::{Default, Plan, FullAuto}` |
| 配置结构 | `PermissionSettings` — mode、allowedTools、deniedTools、pathRules、deniedCommands |
| 决策结果 | `PermissionDecision` — allowed、requiresConfirmation、reason |
| 权限判定 | `PermissionChecker::evaluate()` — 按 1.敏感路径硬拒绝→ 2.denied_tools→ 3.allowed_tools→ 4.path rules→ 5.command rules→ 6.模式判定 |
| 敏感路径 | `.ssh/*`、`.aws/credentials`、`.aws/config`、`.kube/config`、`.docker/config.json` 等，Windows 下 `%USERPROFILE%` 等价路径 |
| 危险命令 | 内置 `rm -rf /`、`del /s /q C:\`、`DROP DATABASE` 等模式 |
| 只读自动允许 | 权限系统通过工具报告的 `is_read_only()` 判断 |
| session 授权 | 支持 `once` / `always` 粒度授权，写入 session 状态 |
| 权限对话框集成 | 在 TUI 中以 modal 形式唤起用户确认，或通过 `permission_prompt()` callback |

## 决策顺序（敏感路径优先）

```
1. 敏感路径硬拒绝（不可被任何配置绕过）
2. denied_tools 列表
3. allowed_tools 列表
4. path deny rules
5. denied command patterns (glob/regex)
6. FullAuto → 直接允许
7. 只读工具 → 自动允许
8. Plan 模式 → 阻止写操作
9. Default 模式 → 写操作需要确认
```

## Tool 与 Permission 集成

在 `Engine::execute_tool_use()` 中：

```
ToolUseBlock
  → 提取 PermissionTarget (path / command)
  → PermissionChecker::evaluate()
  → 若需确认 → UI 弹窗 / callback
  → 拒绝 → 返回 is_error=true 的 ToolResultBlock
  → 允许 → 执行工具
```

## Swarm 权限同步

暂不在当前 C++ 实现范围内。当前 worker 使用只读受限权限。

## 测试覆盖

`tests/permission_tests.cpp` 覆盖：三种模式判定、敏感路径拒绝、`denied_tools` 优先、`allowed_tools` bypass、路径规则、命令规则、符号链接绕过检测、session 授权。
