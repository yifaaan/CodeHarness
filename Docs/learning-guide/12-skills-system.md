# 第12章：Skills 模块详解

> Skills 模块让用户和模型可以复用预定义的提示词/工作流片段。本模块负责发现、解析、注册并激活 Skill。

## 1. 为什么需要 Skills？

### 1.1 复用需求

**用户可能想要**：

- **领域专家提示**：把"代码审查清单"、"提交信息规范"、"测试编写指南"沉淀成可复用片段
- **工作流模板**：把多步操作（如"修复 lint 错误的完整流程"）封装成一次调用
- **项目专属约定**：每个项目自己的规范、脚本、术语表，随仓库分发
- **团队共享**：用户级 Skill 跨项目可用，项目级 Skill 随仓库生效

### 1.2 没有 Skills 的问题

```cpp
// ❌ 问题：每次都要把同样的提示词手敲进 prompt
agent.Prompt(R"(你是一个代码审查专家。请按以下清单审查：
1. 命名是否清晰
2. 是否有边界条件遗漏
3. 测试覆盖是否充分
...（200 行清单）
现在审查这段代码：)" + code);

// 问题：
// 1. 提示词冗长，每次都要重复
// 2. 团队成员各写各的，不一致
// 3. 模型不知道有哪些可用模板，无法主动调用
```

### 1.3 Skills 的解决方案

```text
# <cwd>/.agents/skills/code-review/SKILL.md
---
name: code-review
description: 按团队清单审查代码变更
type: inline
when_to_use: 当用户请求代码审查或审查 PR 时
disable_model_invocation: false
---

你是一个代码审查专家。请按以下清单审查 $ARGUMENTS：
1. 命名是否清晰
2. 是否有边界条件遗漏
...
```

```bash
# 用户主动调用
codeharness_cli --skill code-review:src/engine/loop.cpp --prompt "请审查"

# 或模型自动判断需要时，通过 skill 工具调用
# skill({ "name": "code-review", "args": "src/engine/loop.cpp" })
```

**好处**：
1. **提示词复用**：写一次，多处调用
2. **项目分发**：`<cwd>/.agents/skills/` 随仓库走，团队自动获得
3. **模型可见**：可用 Skill 列表注入 system prompt，模型能主动判断何时调用
4. **参数化**：`$ARGUMENTS` / `$0` / `$name` 占位符支持运行时填参

---

## 2. 核心类型

### 2.1 类型定义

**位置**：`Source/CodeHarness/Skills/SkillTypes.h`

```cpp
// Skill 的来源，决定优先级与可见性
enum class SkillSource
{
    Project,  // <cwd>/.agents/skills —— 最高优先级，随仓库
    User,     // ~/.agents/skills     —— 用户级，跨项目
    Extra,    // [skills].extra_skill_dirs —— 显式额外目录
    Builtin,  // 预留：内置打包技能（当前未填充）
};

// Skill 的激活目标——决定渲染后的内容去哪里
enum class SkillType
{
    Prompt,  // 渲染后注入当轮 system prompt（作为系统级指导）
    Inline,  // 渲染后作为 user 消息追加（默认，模型/用户调用主路径）
    Flow,    // 多步工作流（解析存储，行为暂回退为 inline）
};

// 一个 Skill 的完整定义
struct SkillDefinition
{
    std::string name;         // 唯一键（来自 frontmatter 或文件/目录名）
    std::string description;  // 给模型看的简介
    std::string path;         // 源文件路径
    std::string dir;          // 所属目录（用于 ${KIMI_SKILL_DIR}）
    std::string content;      // frontmatter 之后的正文
    SkillMetadata metadata;   // 解析自 YAML frontmatter
    SkillSource source;       // 发现来源
};

// frontmatter 中可声明的元数据
struct SkillMetadata
{
    std::string name;
    std::string description;
    SkillType type = SkillType::Prompt;
    std::optional<std::string> whenToUse;        // 模型何时该用的提示
    bool disableModelInvocation = false;         // 是否对模型隐藏
    std::vector<std::string> arguments;          // 命名参数列表
    std::optional<std::string> model;            // 指定 profile（预留）
};
```

### 2.2 SkillType 决定激活去向

| 类型 | 渲染结果去向 | 典型场景 |
|------|------------|---------|
| **`Inline`**（默认） | 追加为 **user** 消息 | 模型/用户调用主路径；Skill 作为"额外指令"进入对话 |
| **`Prompt`** | 注入当轮 **system prompt** | 需要"全局指导"语义的 Skill（一次性的、当轮生效） |
| `Flow` | （回退为 inline + 警告） | 多步工作流——当前仅解析存储，未实现执行引擎 |

> **关键区分**：`Inline` 让 Skill 内容成为"用户说的话"，模型把它当对话一部分；`Prompt` 让 Skill 内容成为"系统设定"，模型把它当背景规则。

---

## 3. 数据流：从磁盘到激活

```
config.toml [skills]          <cwd>/.agents/skills/    ~/.agents/skills/
    │                                │                       │
    │  allow_project_skills          │                       │
    │  extra_skill_dirs              ▼                       ▼
    │                     ┌─────────────────────────────────────┐
    └────────────────────►│ SkillScanner::Scan(roots, host)      │
                          │  递归遍历（max depth 8）              │
                          │  识别 SKILL.md（目录式）/ *.md（散文件）│
                          └──────────────────┬──────────────────┘
                                             │ SkillDefinition 列表
                                             ▼
                          ┌─────────────────────────────────────┐
                          │ SkillRegistry                        │
                          │  Register（first-registered-wins）    │
                          │  project > user > extra 优先级        │
                          └──────────────────┬──────────────────┘
                                             │
              ┌──────────────────────────────┼──────────────────────┐
              │                              │                      │
              ▼                              ▼                      ▼
   RenderSkillIndex()            RenderSkillPrompt()            Activate(payload)
   （模型可见清单）              （变量展开）                    （深度守卫 + 路由）
              │                              │                      │
              ▼                              │                      ▼
   注入 system prompt                       │           ┌────────────────────┐
   （每轮重建）                              │           │ Inline → message   │
                                            │           │ Prompt  → system   │
                                            │           │ Flow   → 回退 inline│
                                            │           └────────────────────┘
```

### 3.1 两个激活入口

```
用户路径                              模型路径
──────                              ──────
--skill code-review:file.cpp         skill({name,args})
        │                                  │
        ▼                                  ▼
Agent::ActivateSkill(name, args)     SkillTool::Execute
  origin = UserSlash                   origin = ModelTool
  depth = 0                            depth = 0
        │                                  │
        └──────────────┬───────────────────┘
                       ▼
            SkillManager::Activate(payload)
            （统一入口）
```

两条路径都汇入 `SkillManager::Activate`，唯一区别是 `SkillOrigin`（用于将来区分审计/限流），当前不影响行为。

---

## 4. 关键设计

### 4.1 优先级：first-registered-wins

```cpp
// SkillRegistry.cpp
void SkillRegistry::Register(SkillDefinition skill)
{
    if (skill.name.empty()) return;
    if (skills_.find(skill.name) == skills_.end())  // 已存在则忽略
    {
        skills_.emplace(skill.name, std::move(skill));
    }
}
```

因为 `ResolveSkillRoots` 按 `[Project, User, Extra]` 顺序追加，且 `LoadRoots` 顺序 `Register`，所以**同名的 Skill，project 根里的先注册并胜出**。这让项目级 Skill 能覆盖用户级——团队约定优先于个人偏好。

### 4.2 变量展开

`RenderSkillPrompt` 把正文里的占位符替换为运行时值：

| 占位符 | 替换为 | 示例 |
|--------|-------|------|
| `$ARGUMENTS` | 原始参数串（未拆分） | `--skill review:a.cpp b.cpp` → `a.cpp b.cpp` |
| `$0`, `$1`, … | 按空格/引号拆分的位置参数 | `a.cpp b.cpp` → `$0=a.cpp`, `$1=b.cpp` |
| `$<argname>` | `metadata.arguments` 里命名的参数 | `arguments=["target"]` → `$target` |
| `${KIMI_SKILL_DIR}` | Skill 所属目录 | 用于引用 Skill 自带的辅助文件 |
| `${KIMI_SESSION_ID}` | 当前会话 ID | 用于日志/产物隔离 |

> 拆分器是 `SplitArgs`：支持双引号包裹含空格的参数（`"path with spaces"`），最多 100 个位置参数。

### 4.3 让模型"看见"技能

这是本模块与早期草稿最大的区别。`whenToUse` 和 `disableModel_invocation` **现在被真正消费**：

```cpp
// SkillRegistry::RenderSkillIndex —— 只列出 !disableModelInvocation 的技能
// 每条带上 name / type / description / whenToUse
// 输出形如：
//   ## Available skills
//   - **code-review** (inline): 按团队清单审查代码变更
//     When to use: 当用户请求代码审查或审查 PR 时

// Agent::Prompt —— 每轮重建 system prompt
std::string effectiveSystemPrompt = config.systemPrompt;  // 基底
if (skillRegistry != nullptr)
{
    effectiveSystemPrompt += skillRegistry->RenderSkillIndex();  // 技能清单
    if (!pendingSystemSkillContent.empty())                       // 当轮 prompt 技能
    {
        effectiveSystemPrompt += "\n\n" + pendingSystemSkillContent;
        pendingSystemSkillContent.clear();  // 每轮排空，避免泄漏到后续轮
    }
}
```

**为什么每轮重建？** 因为 prompt 型 Skill 是"当轮生效"的（用户在某一轮激活它）。若不排空，它会污染后续所有轮的 system prompt。`Agent` 在 `SetSkillManager` 时快照了原始 system prompt（`baseSystemPrompt`），每轮从基底重建，保证幂等。

### 4.4 深度守卫（防递归）

```cpp
// SkillManager.h
static constexpr int MAX_DEPTH = 3;

// Activate 内
if (payload.depth > MAX_DEPTH)
    return absl::ResourceExhaustedError("Maximum skill recursion depth exceeded");
```

当前两个入口（用户/模型）都固定 `depth = 0`，所以守卫是**前瞻性**的——为将来的嵌套 Skill 链式调用（`SkillOrigin::NestedSkill`）预留。

### 4.5 与配置的对接

```toml
# config.toml
[skills]
allow_project_skills = true                    # 默认 true；false 则跳过 <cwd>/.agents/skills
extra_skill_dirs = ["/opt/team-skills", "~/lib/skills"]  # 追加为 Extra 来源
```

- `Config.h`：`struct SkillConfig { bool allowProjectSkills = true; std::vector<std::string> extraSkillDirs; };`
- `ConfigManager`：解析 `[skills]` 表；序列化时仅在非默认值时输出（保持 round-trip 干净）。
- `RunPrompt::Run`：**只读一次 config.toml**，把 `cfg.skills` 同时喂给 provider 解析和 `ResolveSkillRoots`（避免重复磁盘读）。

---

## 5. 代码走读

### 5.1 文件清单

| 文件 | 职责 |
|------|------|
| `Skills/SkillTypes.h/.cpp` | 数据模型 + 枚举字符串转换 |
| `Skills/SkillParser.h/.cpp` | YAML frontmatter + markdown 正文解析（`yaml-cpp`） |
| `Skills/SkillScanner.h/.cpp` | 递归目录扫描（通过 `Host` 抽象） |
| `Skills/SkillRegistry.h/.cpp` | 内存存储 + 优先级 + 变量展开 + 清单渲染 |
| `Skills/SkillManager.h/.cpp` | 激活编排 + 深度守卫 + 类型路由 |
| `Skills/SkillTool.h/.cpp` | 把 Skill 暴露为模型可调用的 `skill` 工具 |

### 5.2 接入点（非 Skills 目录）

- `Agent/Agent.h/.cpp` — `SetSkillManager`（注入两个 callback）、`ActivateSkill`、`Prompt`（重建 system prompt）
- `Cli/RunPrompt.cpp` — `ResolveSkillRoots`、`Run`（共享 cfg）、`--skill` 解析
- `Cli/CliParser.cpp` / `CliOptions.h` — `-s,--skill` 选项
- `Config/Config.h` / `ConfigManager.cpp` — `[skills]` 表

### 5.3 激活路径（核心调用）

```cpp
// SkillManager::Activate —— 路由核心
auto* skill = registry->GetSkill(payload.name);
auto rendered = registry->RenderSkillPrompt(*skill, payload.args, sessionId);

// 按 SkillType 选 sink
const auto* sink = (skill->metadata.type == SkillType::Prompt && appendSystem)
                   ? &appendSystem    // prompt 型且有 system callback
                   : &appendMessage;  // inline 型，或 prompt 型但无 system callback

if (skill->metadata.type == SkillType::Flow)
    spdlog::warn("skills: 'flow' type is not fully implemented; activating '{}' as inline", payload.name);

if (sink && *sink)
    return (*sink)(std::span<const char>(rendered...));
```

注意 **prompt 型若无 system callback 会优雅回退到 message callback**——这让 SkillManager 在未接入 Agent 的纯测试场景下仍可工作。

---

## 6. 测试

测试位于 `Test/Skills/`，覆盖五个组件：

| 文件 | 覆盖点 |
|------|-------|
| `SkillTypesTest.cpp` | 枚举字符串转换 |
| `SkillParserTest.cpp` | frontmatter 解析、目录式/散文件命名、正文提取 |
| `SkillRegistryTest.cpp` | 注册/优先级/列表/`RenderSkillPrompt` 变量展开/`RenderSkillIndex` |
| `SkillManagerTest.cpp` | 激活、深度守卫、**prompt↔inline 路由**、回退 |
| `SkillToolTest.cpp` | 工具参数校验、manager 接线 |

新增的关键测试：
- **`RenderSkillIndex`**：空注册表返回空串；全为非可调用技能返回空串；正确列出可调用技能并过滤 `disableModelInvocation`；包含类型标记。
- **prompt/inline 路由**：inline 型走 message callback、prompt 型走 system callback；prompt 型无 system callback 时回退到 message。

---

## 7. 边界与未来

**当前已实现（稳定）**：
- 解析（YAML frontmatter + markdown）、发现（递归扫描，四种来源优先级）、注册（first-wins）、变量展开（5 类占位符）、激活（两入口统一）、system-prompt 注入（让模型看见技能）、prompt/inline 类型区分。

**明确延迟（不在本次范围）**：
- `SkillType::Flow` 多步工作流执行引擎（参考设计标注"未完全实现"）。
- 嵌套 Skill 链式调用（`SkillOrigin::NestedSkill` + depth 递增；守卫已就位但路径未接）。
- `SkillSource::Builtin` / 打包内置技能（如 `graphify`）。
- 交互式 TUI `/skill-name` 斜杠命令（依赖 #14 TUI 层；当前仅 `--skill` CLI）。
- MCP 集成（独立的 #13 计划）。

**相关文档**：
- 用户指南：[docs/customization/skills.md](../customization/skills.md)
- 执行计划：[docs/plan/re-build/exec-plans/active/12-skill-system.md](../plan/re-build/exec-plans/active/12-skill-system.md)
- 设计参考：[docs/plan/re-build/references/skill-system.md](../plan/re-build/references/skill-system.md)
