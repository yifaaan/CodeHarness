# tools/ — 工具系统模块

## 设计目标

实现 LLM 可以调用的工具（function calling）框架。提供工具注册、执行、权限目标提取等功能，以及一组内置工具。

## 架构

```
Tool (抽象接口)
  ├─ name()             ← 工具名称（LLM 通过名称调用）
  ├─ description()      ← 工具描述（注入 system prompt）
  ├─ is_read_only()     ← 是否只读（影响权限决策）
  ├─ permission_target()← 提取权限目标（路径/命令）
  └─ execute(context)   ← 核心执行逻辑

ToolRegistry
  ├─ register(tool) → 注册工具
  ├─ execute(name, input) → 按名称调度执行
  └─ names() → 列出所有工具名称
```

### 内置工具

| 工具 | 类名 | 是否只读 | 功能 |
|------|------|----------|------|
| `read` | `ReadFileTool` | ✅ | 安全读取文件（路径验证） |
| `write` | `WriteFileTool` | ❌ | 原子写入文件（自动创建目录） |
| `edit` | `EditFileTool` | ❌ | 字符串替换编辑 |
| `bash` | `BashTool` | ❌ | 执行 shell 命令 |
| `glob` | `GlobTool` | ✅ | 文件模式匹配 |
| `grep` | `GrepTool` | ✅ | 文件内容正则搜索 |
| `skill` | `SkillTool` | ✅ | 加载技能为工具 |

### 辅助类型

| 类型 | 职责 |
|------|------|
| `PermissionTarget` | 权限目标：path + command |
| `ToolContext` | 工具执行上下文（工作目录、权限模式等） |
| `ToolRequest` / `ToolResponse` | 工具请求/响应模型 |
| `resolve_workspace_path()` | 工作空间路径安全解析 |
| `read_text_file()` / `atomic_write_text_file()` | 原子文件 I/O |

## 设计要点

- 所有工具实现 `Tool` 接口，通过 `ToolRegistry` 统一注册和调度
- 文件路径通过 `resolve_workspace_path()` 校验，防止路径逃逸
- 写工具（`write`、`edit`、`bash`）使用原子写入模式（临时文件 + rename）
- `PermissionTarget` 是工具与权限模块之间的桥梁——提取工具的目标以便权限评估
- 工具执行失败不会崩溃，而是通过 `ToolResponse{is_error=true}` 返回给模型

## 初学者指南

- 这是 LLM 与外部世界交互的接口层——每个工具都对应一个能力
- 阅读顺序：先看 `tool.h` 理解接口设计，再看某个具体工具（如 `read_file_tool.h`）
- 添加新工具：继承 `Tool` → 实现四个虚函数 → 在 `ToolRegistry` 中注册
- 权限检查路径：`Engine` 收到 `ToolUseBlock` → `ToolRegistry::execute()` → 内部调用 `PermissionChecker` → 执行或拒绝
- 文件工具使用 `workspace_path.h` 确保安全性：禁止访问工作目录之外的文件
