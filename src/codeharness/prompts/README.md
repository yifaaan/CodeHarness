# prompts/ — 系统提示词组装模块

## 设计目标

动态组装发送给 LLM 的 system prompt。将环境信息、技能指令、项目上下文、记忆和权限模式等信息编译成一条完整的系统提示。

## 架构

```
SystemPromptBuilder::build(request)
  └─ PromptBuildRequest
       ├─ detect_environment()             ← OS、Shell、cwd、git 状态
       ├─ skills                           ← 当前启用的技能
       ├─ commands                         ← 可用斜杠命令
       ├─ project_context_files            ← AGENTS.md、CLAUDE.md 等
       ├─ memories                         ← 记忆存储中的相关条目
       └─ permission_mode                  ← 当前权限模式

ProjectContextLoader::load(cwd)
  └─ 从 cwd 向上遍历到 git root
       └─ 发现并加载 AGENTS.md、CLAUDE.md
            └─ 受 max_total_chars 限制
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `SystemPromptBuilder` | 系统提示词组装器 |
| `PromptBuildRequest` | 构建请求，包含所有上下文输入 |
| `ProjectContextLoader` | 项目上下文文件加载器 |

## 设计要点

- `detect_environment()` 自动检测运行环境，返回结构的 JSON 信息
- 项目上下文文件从当前目录向上遍历到 git 根目录查找，支持 AGENTS.md 和 CLAUDE.md
- `max_total_chars` 防止上下文溢出
- system prompt 的最终结构：内置指令 → 技能指令 → 项目上下文 → 记忆 → 权限说明

## 初学者指南

- 这是智能体"认识自己"的方式——system prompt 告诉 LLM 它能做什么、在哪里运行
- 如果你想让智能体理解项目的自定义规则，在项目根目录放一个 `AGENTS.md`
- 输出直接作为 `RunRequest.system_prompt` 传给 `Engine`
