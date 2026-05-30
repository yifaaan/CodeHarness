# Skills C++20 重写方案

Skills 是按需加载的 Markdown 知识包。它不是可执行代码，而是给模型看的工作说明、流程、约束或领域知识。

上游关键文件：

- `docs/OpenHarness/src/openharness/skills/types.py`
- `docs/OpenHarness/src/openharness/skills/loader.py`
- `docs/OpenHarness/src/openharness/skills/registry.py`
- `docs/OpenHarness/src/openharness/skills/_frontmatter.py`
- `docs/OpenHarness/src/openharness/skills/bundled/content/*.md`

## Skill 文件布局

OpenHarness 支持类似 Claude skills 的布局：

```text
<skill-root>/<skill-name>/SKILL.md
```

文件内容是 Markdown，并可带 YAML frontmatter：

```markdown
---
name: review
description: Review code for correctness, security, and maintainability.
user-invocable: true
argument-hint: FILE_OR_DIFF
---

# Review Skill

Use this when the user asks for a code review...
```

## SkillDefinition

```cpp
struct SkillDefinition {
    std::string name;
    std::string description;
    std::string content;
    std::string source; // bundled | user | project | plugin
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> baseDir;
    std::optional<std::string> commandName;
    std::optional<std::string> displayName;
    std::vector<std::string> aliases;
    bool userInvocable = true;
    bool disableModelInvocation = false;
    std::optional<std::string> model;
    std::optional<std::string> argumentHint;
};
```

## 加载来源

建议按上游顺序加载：

1. bundled skills：内置到可执行文件或安装目录。
2. user skills：`~/.openharness/skills`、`~/.claude/skills`、`~/.agents/skills`。
3. extra skill dirs：例如 ohmo workspace。
4. project skills：从 cwd 向上找 `.openharness/skills`、`.agents/skills`、`.claude/skills`。
5. plugin skills：enabled plugin 贡献的 skills。

后加载的同名 skill 可以覆盖前面的。项目下更靠近 cwd 的 skill 覆盖父目录 skill。

## SkillRegistry

```cpp
class SkillRegistry {
public:
    void registerSkill(SkillDefinition skill);
    const SkillDefinition* get(std::string_view key) const;
    std::vector<SkillDefinition> list() const;

private:
    std::vector<std::shared_ptr<SkillDefinition>> skills_;
    std::unordered_map<std::string, std::shared_ptr<SkillDefinition>> byKey_;
};
```

一个 skill 应按多个 key 注册：

- `name`
- `commandName`
- `displayName`
- `aliases`

例如 skill 名叫 `Code Review`，命令名可以是 `review`。

## Frontmatter parser

第一版可以实现简单 YAML frontmatter parser：

```text
如果文件以 --- 开头：
  找下一个 ---
  中间部分按 key: value 解析
  后面是 Markdown body
否则：
  整个文件是 body
```

如果项目后续已经依赖 `yaml-cpp`，可以直接用它。

## Skill 工具

上游有 `skill` 工具，让模型按需加载 skill 内容。

C++ 可以实现：

```cpp
class SkillTool : public ITool {
public:
    SkillTool(SkillRegistry& registry);
    ToolResult execute(const nlohmann::json& args,
                       const ToolExecutionContext& ctx) override;
};
```

输入：

```json
{"name":"review"}
```

输出：skill Markdown 内容。

## Skill 作为 Slash Command

`userInvocable=true` 的 skill 可以暴露成 `/review` 这样的命令。

命令执行逻辑：

1. 用户输入 `/review src/main.cpp`。
2. 查找 `review` skill。
3. 用 skill 内容构造 prompt。
4. 替换变量：`$ARGUMENTS`、`${ARGUMENTS}`、`${CLAUDE_SKILL_DIR}`。
5. 如果 `disableModelInvocation=true`，只显示内容。
6. 否则把 prompt 提交给 QueryEngine。

## 安全注意

- Project skills 默认可以启用，但对不可信项目应允许关闭。
- Skill 路径不能允许 `..` 逃逸。
- Skill 是 prompt，不是代码，但仍可能 prompt injection。UI 应展示来源。
- Plugin skills 只来自 enabled plugins。

## 第一版路线

1. 内置 3 个 bundled skills：`plan`、`review`、`debug`。
2. 加载 `~/.openharness/skills/<name>/SKILL.md`。
3. 实现 `SkillRegistry`。
4. 实现 `/skills` 列表。
5. 实现 `skill` 工具。
6. 实现 user-invocable skill slash command。

## 测试清单

- 带 frontmatter 的 SKILL.md 能解析。
- 无 frontmatter 的 SKILL.md 能解析。
- user skill 覆盖 bundled skill。
- 近 cwd project skill 覆盖父目录 skill。
- aliases 可查找。
- `/skill args` 能替换 `$ARGUMENTS`。
