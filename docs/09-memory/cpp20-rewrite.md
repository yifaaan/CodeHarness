# Memory C++20 重写方案

Memory 是长期上下文系统。它把项目知识、用户偏好、反馈、参考资料保存为 Markdown 文件，并在相关 prompt 中注入。

上游关键文件：

- `docs/OpenHarness/src/openharness/memory/paths.py`
- `docs/OpenHarness/src/openharness/memory/schema.py`
- `docs/OpenHarness/src/openharness/memory/manager.py`
- `docs/OpenHarness/src/openharness/memory/scan.py`
- `docs/OpenHarness/src/openharness/memory/search.py`
- `docs/OpenHarness/src/openharness/memory/relevance.py`
- `docs/OpenHarness/src/openharness/memory/usage.py`
- `docs/OpenHarness/src/openharness/memory/team.py`
- `docs/OpenHarness/src/openharness/memory/agent.py`

## Memory 的本质

Memory 不是神秘数据库。OpenHarness 的 memory 本质是：

```text
一组带 YAML frontmatter 的 Markdown 笔记 + 一个 MEMORY.md 索引 + usage_index.json
```

这对初学者友好，因为用户可以直接打开文件阅读和编辑。

## 路径设计

项目 memory 路径：

```text
~/.openharness/data/memory/<project-name>-<sha1(cwd)前12位>/
```

目录里有：

```text
MEMORY.md
topic-a.md
topic-b.md
usage_index.json
.memory.lock
```

## Memory metadata

```cpp
struct MemoryMetadata {
    int schemaVersion = 1;
    std::string id;
    std::string name;
    std::string description;
    std::string type = "project";     // user | feedback | project | reference
    std::string scope = "project";    // private | project | team
    std::string category = "knowledge";
    int importance = 0;
    std::string source = "manual";
    std::string signature;
    std::string createdAt;
    std::string updatedAt;
    std::optional<int> ttlDays;
    bool disabled = false;
    std::vector<std::string> supersedes;
    std::vector<std::string> tags;
};

struct MemoryHeader {
    std::filesystem::path path;
    std::string title;
    std::string description;
    std::string bodyPreview;
    std::time_t modifiedAt;
    MemoryMetadata metadata;
};
```

## 文件格式

```markdown
---
schema_version: 1
id: mem-abc123
name: Build Notes
description: CMake build notes for CodeHarness.
type: project
scope: project
category: knowledge
importance: 1
source: manual
signature: ...
created_at: 2026-05-30T00:00:00Z
updated_at: 2026-05-30T00:00:00Z
ttl_days: null
disabled: false
tags: [build, cmake]
---

Use CMake as the build tool. Do not generate go.mod.
```

## MemoryStore

```cpp
class MemoryStore {
public:
    explicit MemoryStore(std::filesystem::path root);

    std::filesystem::path add(std::string title,
                              std::string body,
                              MemoryMetadata metadata);

    bool softRemove(std::string_view idOrName);
    std::vector<MemoryHeader> scan(bool includeDisabled = false) const;
    std::vector<MemoryHeader> search(std::string_view query, size_t limit = 5);
};
```

## 添加 memory 的流程

1. 规范化 title，生成文件名。
2. 加 `.memory.lock`。
3. 计算内容签名，避免重复。
4. 写入 Markdown + frontmatter。
5. 更新 `MEMORY.md` 索引。
6. 释放锁。

写文件建议 tmp + rename：

```text
topic.md.tmp -> fsync -> rename topic.md
```

## 删除 memory

上游是软删除：

- 设置 `disabled: true`。
- 从 `MEMORY.md` 索引移除。
- 文件仍保留。

这样可以避免误删知识。

## 搜索相关性

上游使用简单启发式，不依赖向量数据库。评分因素：

- query token 命中 metadata，权重高。
- query token 命中 body preview。
- importance 加分。
- use_count 加分。
- 最近更新加分。

C++ 第一版可以实现同样逻辑。中文/CJK 可以按字符 token，英文按单词 token。

## Usage index

```json
{
  "version": 1,
  "memories": {
    "mem-abc": {
      "last_used_at": "...",
      "use_count": 3,
      "path": "build-notes.md"
    }
  }
}
```

用途：

- 搜索排序。
- 找长期未使用的 memory。
- 后续 auto-dream 或整理。

## Prompt 注入

Prompt builder 可以注入：

```markdown
# Relevant Memories

## Build Notes

Use CMake as the build tool...
```

不要每次注入所有 memory，只注入与当前 prompt 相关的几条。

## Team memory 安全

上游 team memory 写入前会扫描 secret。C++ 也应保留：

- private key。
- AWS key。
- GitHub token。
- OpenAI/Anthropic key。
- generic secret assignment。

## 第一版路线

1. `MemoryStore.add/scan/softRemove`。
2. `MEMORY.md` 索引。
3. 简单 search。
4. `/memory list/add/remove`。
5. Prompt 注入 relevant memories。
6. Usage index。

## 不建议第一版做

- SQLite。
- 向量数据库。
- 自动总结全部会话。
- 多 agent team memory。

先把 Markdown memory 做稳定。
