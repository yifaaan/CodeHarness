# skills/ — 技能系统模块

## 设计目标

将领域专业知识打包为"技能"，注入到 system prompt 中，让 LLM 获得特定领域的指导。技能是 CodeHarness 实现专业化的核心机制。

## 架构

```
SkillDefinition
  ├─ name / description / content    ← 技能元数据和正文
  ├─ aliases / source                ← 别名和来源
  └─ model                           ← 适用的模型（可选）

SkillRegistry
  ├─ register(skill) / find(name) / names()
  └─ 按名称查找和遍历技能

SkillLoader
  └─ 加载链：内置 → 用户 → 额外 → 项目（优先级覆盖）
       └─ load_skill_registry_with_plugins()  ← 与插件系统集成

BundledSkills
  └─ 加载内置技能（bundled/content/*.md）
       ├─ commit.md, debug.md, diagnose.md
       ├─ plan.md, review.md, simplify.md
       ├─ skill-creator.md, test.md

SkillYaml
  └─ YAML frontmatter 解析工具（基于 yaml-cpp）

SkillTool
  └─ 将技能包装为 Tool，供 LLM 在运行时动态加载切换
```

### 关键类型

| 类型 | 职责 |
|------|------|
| `SkillDefinition` | 技能定义，包含名称、描述、正文内容等 |
| `SkillRegistry` | 技能注册中心，支持查找和遍历 |
| `SkillLoader` | 技能加载链，处理优先级覆盖 |
| `SkillTool` | 将技能暴露为 LLM 可调用的工具 |

## 设计要点

- 技能是 markdown 文件，正文直接注入 system prompt
- 优先级：内置 < 用户 < 额外 < 项目，高优先级覆盖低优先级
- 技能可以通过 `SkillTool` 在运行时被 LLM 动态加载
- 插件可以携带自己的技能集（通过 `plugin_loader` 的 `load_skill_registry_with_plugins()`）

## 初学者指南

- 技能 ≈ 给 LLM 的领域指令手册
- 内置技能在 `skills/bundled/content/` 下，每个是一个 markdown 文件
- 如果你想为项目添加自定义技能，在项目中创建 `.md` 文件并配置 `AGENTS.md` 即可
- 加载流程：`RuntimeBundle` 初始化 → `load_skill_registry_with_plugins()` → 加载内置+用户+项目技能 → 注册到引擎
