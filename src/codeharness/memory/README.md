# memory/ — 记忆存储模块

## 设计目标

为智能体提供长期记忆能力，存储和检索项目相关的知识片段。基于文件的持久化存储，支持全文搜索。

## 架构

```
MemoryStore
  ├─ add(entry)          ← 新建记忆条目（存为 Markdown 文件）
  ├─ scan()              ← 列出所有条目标题（可过滤）
  ├─ search(query)       ← 全文搜索记忆正文
  ├─ read(header)        ← 读取单个条目完整内容
  └─ soft_remove(header) ← 软删除（标记为不可用，不删除文件）

MemoryEntry
  ├─ header: title + description + metadata
  └─ body: 正文内容
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `MemoryStore` | 记忆存储管理器，以文件为存储单元 |
| `MemoryEntry` | 单条记忆：header（标题、描述、元数据）+ body |
| `project_memory_dir()` | 返回当前项目的记忆目录路径 |

## 设计要点

- 每条记忆是一个独立 Markdown 文件
- `search()` 基于全文搜索（遍历文件正文匹配关键词）
- `soft_remove()` 不删文件，只标记禁用——避免误删
- 记忆范围限定在项目内，不同项目的记忆互不干扰

## 初学者指南

- 记忆模块让智能体在跨会话场景中记住信息
- 系统提示词组装时（`prompts/` 模块），记忆会被注入到 system prompt 中
- 存储路径：`{data_dir}/projects/{project_name}/memory/`
