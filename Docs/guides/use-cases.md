# 常见使用案例

## 理解陌生项目

```powershell
codeharness -p "阅读这个仓库的入口和测试，概括主要模块"
```

Agent 会优先使用 `glob`、`grep`、`read_file` 等只读工具。

## 实现新功能

在 TUI 中描述目标，让 Agent 先分析相关代码，再通过 `edit_file` 或 `write_file` 修改。默认权限模式会在写入前请求确认。

## 修复 bug

```powershell
codeharness -p "复现最近的测试失败，定位原因并给出修复"
```

如果需要运行命令，Agent 会使用 `bash`。命令执行类工具默认需要审批。

## 写测试与重构

适合让 Agent 搜索现有测试风格，添加 focused tests，再运行对应测试命令。CodeHarness 的工具失败会以 `is_error=true` 的工具结果回填给模型，agent loop 可以继续调整。

## 后台任务

长时间命令或子 Agent 工作可以通过任务工具管理：

- `task_create`
- `task_list`
- `task_get`
- `task_output`
- `task_stop`
- `agent`

## 维护文档

文档任务适合先用 `--plan` 或 `/plan` 做只读盘点，再切回 `/act` 执行修改。
