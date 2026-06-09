# Memory C++20 实现参考

Memory 模块的 C++20 实现已完成，代码见 `src/codeharness/memory/memory_store.h/.cpp`。

## 已实现的能力

| 能力 | 说明 |
| --- | --- |
| `MemoryMetadata` 结构 | schemaVersion、id、name、description、type、scope、importance、source、signature、ttlDays、disabled、tags、supersedes 等 |
| `MemoryHeader` 结构 | path、title、description、bodyPreview、modifiedAt、metadata |
| `MemoryStore` | `add(title, body, metadata)` → 写入 Markdown + YAML frontmatter，tmp + rename；`scan()` → 扫描所有 memory 文件；`search(query, limit)` → 启发式相关性搜索；`soft_remove(id)` → 标记 `disabled: true`；`read(id)` → 读取完整内容 |
| MEMORY.md 索引 | 自动维护索引文件，列出所有活跃 memory |
| Usage index | `usage_index.json` — 记录 use_count、last_used_at |
| 相关性搜索 | 基于 query token 命中 metadata/body preview + importance/use_count/recency 评分 |
| `/memory` 命令 | 支持 `/memory list`、`/memory add`、`/memory remove` |
| Prompt 注入 | `SystemPromptBuilder` 只注入相关 memories，不全部注入 |

## 文件格式

```
~/.codeharness/data/memory/<project-hash>/
  MEMORY.md           索引
  topic-name.md       memory 内容（YAML frontmatter + Markdown body）
  usage_index.json    使用统计
```

## 设计要点

- Memory = Markdown 笔记 + YAML frontmatter，用户可直接编辑
- 软删除（设置 `disabled: true`），文件保留但不在索引中
- Search 使用简单启发式（不依赖向量数据库），支持中文（字符 token）和英文（单词 token）
- 写入使用 tmp + rename，避免写入中断破坏文件
- 内容签名防重复

## 暂不实现的功能

以下功能暂不在当前 C++ 实现范围内：

- SQLite 索引
- 向量数据库
- 自动总结会话
- 多 agent team memory
- Team memory secret 扫描
