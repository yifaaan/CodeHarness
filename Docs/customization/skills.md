# Agent Skills

Skills 是可加载的提示词/工作流片段。CodeHarness 会发现磁盘上的 Skill，把可由模型调用的 Skill 列表注入 system prompt，并提供两条调用路径：用户通过 `--skill` 主动调用，或模型通过 `skill` 工具自动调用。

## Skill 存放位置

CodeHarness 按以下顺序扫描 Skill 根目录，**同名 Skill 先注册者胜出**，因此 project 根优先级最高：

| 来源 | 路径 | 优先级 | 是否默认开启 |
|------|------|--------|------------|
| **Project** | `<cwd>/.agents/skills` | 最高（随仓库分发，团队约定） | 是（可关） |
| **User** | `~/.agents/skills` | 次之（用户级，跨项目） | 是 |
| **Extra** | `[skills].extra_skill_dirs` 中列出的目录 | 最低 | 显式配置时 |

项目级扫描由配置开关控制（见下文）。

## Skill 定义文件

Skill 是一个 markdown 文件，带 YAML frontmatter。两种文件形态都支持：

```text
<root>/my-skill/SKILL.md      # 目录式：skill 名 = 目录名
<root>/my-skill.md             # 散文件式：skill 名 = 文件名（去扩展名）
```

### Frontmatter 字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | Skill 名称。省略时从目录名/文件名推导 |
| `description` | string | 给模型看的简介（出现在 Skill 清单中） |
| `type` | string | `prompt` / `inline` / `flow`（见下） |
| `when_to_use` | string | 模型何时该用的提示。出现在 Skill 清单中 |
| `disable_model_invocation` | bool | 是否对模型隐藏（仅用户可调）。默认 `false` |
| `arguments` | string[] | 命名参数列表，用于 `$<argname>` 展开 |
| `model` | string | 指定 profile（预留，当前不生效） |

> 说明：`type` 决定渲染结果的去向——`inline`（默认）作为 user 消息进入对话；`prompt` 注入当轮的 system prompt 作为系统级指导；`flow` 当前仅解析存储，行为回退为 inline（多步工作流尚未实现）。

### 完整示例

```markdown
---
name: code-review
description: 按团队清单审查代码变更
type: inline
when_to_use: 当用户请求代码审查或审查 PR 时
disable_model_invocation: false
arguments:
  - target
---

你是一个代码审查专家。请按以下清单审查 `$target`（路径：$ARGUMENTS）：

1. 命名是否清晰
2. 是否有边界条件遗漏
3. 测试覆盖是否充分

Skill 自带的辅助资料位于：${KIMI_SKILL_DIR}
```

## 调用 Skill

### 方式一：用户主动调用（`--skill`）

```bash
codeharness_cli --skill code-review:src/engine/loop.cpp --prompt "请开始审查"
```

`--skill` 的格式为 `name[:args]`。冒号后的部分作为原始参数串传给 Skill。

### 方式二：模型自动调用（`skill` 工具）

可由模型调用的 Skill（`disable_model_invocation: false`）会以清单形式注入 system prompt，模型据此判断何时调用 `skill` 工具：

```json
{ "name": "skill", "args": { "name": "code-review", "args": "src/engine/loop.cpp" } }
```

## 参数占位符

Skill 正文支持以下占位符，激活时被替换：

| 占位符 | 替换为 | 示例 |
|--------|-------|------|
| `$ARGUMENTS` | 原始参数串（未拆分） | `--skill s:a.cpp b.cpp` → `a.cpp b.cpp` |
| `$0`, `$1`, … | 按空格/引号拆分的位置参数 | `a.cpp b.cpp` → `$0=a.cpp`, `$1=b.cpp` |
| `$<argname>` | `arguments` 中命名的参数 | `arguments: [target]` → `$target` |
| `${KIMI_SKILL_DIR}` | Skill 所属目录 | 引用 Skill 自带的辅助文件 |
| `${KIMI_SESSION_ID}` | 当前会话 ID | 日志/产物隔离 |

> 拆分支持双引号包裹含空格的参数（`"path with spaces"`），位置参数上限 100 个。

## 配置（`config.toml`）

```toml
[skills]
allow_project_skills = true                       # false 则跳过 <cwd>/.agents/skills
extra_skill_dirs = ["/opt/team-skills", "~/lib/skills"]  # 追加为 Extra 来源
```

- `allow_project_skills`：默认 `true`。设为 `false` 可在受控环境中禁用项目级 Skill（防止仓库注入）。
- `extra_skill_dirs`：追加任意数量的额外扫描目录（来源标记为 `Extra`，优先级最低）。

两个字段都是可选的；省略 `[skills]` 表时，行为等同于默认值（项目级开启、无额外目录）。

## 优先级与冲突

- 同名 Skill：**先注册者胜出**。由于扫描顺序固定为 `[Project, User, Extra]`，项目级 Skill 总是覆盖用户级与额外级——团队约定优先于个人偏好。
- 解析失败的 Skill 会被跳过并记录警告日志，不影响其他 Skill 加载。

## 规划中（尚未实现）

以下能力在设计中提及，但当前版本**未实现**，避免文档与实现漂移：

- `command_name` / `aliases` / `display_name` 等额外元数据字段
- `argument_hint` 参数提示与"无占位符时自动追加 `Arguments: ...`"的回退逻辑
- 交互式 TUI 的 `/skill-name` 斜杠命令（当前仅 `--skill` CLI flag）
- `type: flow` 多步工作流执行引擎
- 嵌套 Skill 链式调用
- 内置/打包 Skill（`Builtin` 来源）
- Skill 市场、版本化、组合

## 相关文档

- 深入讲解：[学习指南第12章](../learning-guide/12-skills-system.md)
- 执行计划：[exec-plans/completed/12-skill-system.md](../plan/re-build/exec-plans/completed/12-skill-system.md)
- 设计参考：[references/skill-system.md](../plan/re-build/references/skill-system.md)
