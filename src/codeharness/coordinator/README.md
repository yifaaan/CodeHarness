# coordinator/ — 多智能体协调模块

## 设计目标

管理多个 LLM 智能体的生命周期和通信。支持创建子代理（subagent）、分配角色、收集任务结果。

## 架构

```
CoordinatorRuntime                          ← 协调器运行时（"init 进程"）
  ├─ AgentDefinitionRegistry                ← 角色定义模板库
  ├─ TaskManager                            ← 本地/远程任务管理
  ├─ TeamLifecycleManager                   ← 团队生命周期
  ├─ Mailbox                                ← 跨进程消息队列
  └─ SubprocessBackend                      ← 子进程管理
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `CoordinatorRuntime`      | 协调器的总控，持有所有子组件 |
| `AgentDefinition`         | 智能体角色模板（名称、描述、允许的工具、模型等），从 YAML frontmatter 加载 |
| `AgentDefinitionRegistry` | 存储角色定义，支持用户>额外>项目三级覆盖 |
| `SubprocessBackend`       | 孵化子进程智能体 |
| `TeammateSpawnConfig`     | 完整的孵化配置（角色、模型、权限、技能等） |
| `SpawnResult`             | 孵化结果（task_id + agent_id） |
| `TaskNotification`        | 任务完成通知，XML 格式，可注入到对话上下文中 |

## 设计要点

- 角色定义存放在 Markdown 文件的 YAML frontmatter 中，遵循优先级：用户 > 额外 > 项目
- 通过 `resolve_spawn_config()` 将 `AgentSpawnRequest` 与 `AgentDefinition` 合并为最终配置
- `SubprocessBackend` 基于 `reproc` 库管理子进程生命周期