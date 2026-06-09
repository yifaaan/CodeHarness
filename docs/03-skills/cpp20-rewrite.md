# Skills C++20 实现参考

Skills 模块的 C++20 实现已完成。代码见 `src/codeharness/skills/`（8 个文件 + 8 个 bundled skills）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| `SkillDefinition` 结构体 | `skills/skill.h` — name、description、content、source、path、aliases 等 |
| `SkillRegistry` | `skills/skill_registry.h/.cpp` — 按 key 注册/查找/列出，支持 name、aliases、displayName |
| Frontmatter YAML 解析 | `skills/skill_yaml.h` — 基于 `yaml-cpp` 解析 `---` frontmatter |
| 多来源加载 | `skills/skill_loader.h/.cpp` — 按 bundled → user → project 顺序加载，同名覆盖 |
| `SkillTool` | `tools/skill_tool.h/.cpp` — 模型通过 `skill` 工具按需加载 skill 完整内容 |
| Bundled skills (8个) | `skills/bundled_skills.h` — plan、review、debug、commit、test、diagnose、simplify、skill-creator |
| 搜索路径 | user: `~/.codeharness/skills`、`~/.openharness/skills`、`~/.claude/skills`、`~/.agents/skills`；project: 从 cwd 向上查找 |

## 设计要点

- Skills 是 Markdown 知识包（给模型看的工作说明/流程/约束），不是可执行代码
- YAML frontmatter 可选；无 frontmatter 的 SKILL.md 将文件名作为 skill name
- 后加载的同名 skill 覆盖先加载的
- `SkillTool` 输入 `{"name":"review"}`，输出 skill Markdown 内容供模型阅读
- `userInvocable=true` 的 skill 可通过 `/技能名` slash command 调用，支持 `$ARGUMENTS` 替换

## 加载顺序

1. Bundled skills（内置）
2. User skills（`~/.codeharness/skills/` 等）
3. Project skills（从 cwd 向上找到 git 根，距 cwd 越近优先级越高）
