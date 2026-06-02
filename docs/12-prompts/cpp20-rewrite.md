# Prompts C++20 重写方案

Prompts 模块负责组装 system prompt。它决定模型在每一轮对话前知道哪些规则、环境、工具说明、记忆和上下文。

上游关键文件：

- `docs/OpenHarness/src/openharness/prompts/system_prompt.py`
- `docs/OpenHarness/src/openharness/prompts/environment.py`
- `docs/OpenHarness/src/openharness/prompts/context.py`
- `docs/OpenHarness/src/openharness/prompts/claudemd.py`
- `docs/OpenHarness/src/openharness/coordinator/coordinator_mode.py`

## Prompt builder 的作用

System prompt 不是一段固定字符串，而是由多个部分拼出来：

- 基础身份和行为规则。
- 当前环境：OS、shell、cwd、git 信息。
- 权限模式：default、plan、full_auto。
- 可用 skills 摘要。
- 可用 commands 摘要。
- CLAUDE.md 或 AGENTS.md 项目说明。
- memory 相关内容。
- coordinator/subagent 规则。
- output style。
- safety reminders。

## 模块设计

```cpp
class EnvironmentDetector {
public:
    EnvironmentInfo detect(const std::filesystem::path& cwd) const;
};

class ClaudeMdLoader {
public:
    std::vector<ContextFile> load(const std::filesystem::path& cwd) const;
};

class SystemPromptBuilder {
public:
    std::string build(const PromptBuildRequest& request) const;
};
```

## PromptBuildRequest

```cpp
struct PromptBuildRequest {
    Settings settings;
    std::filesystem::path cwd;
    std::optional<std::string> latestUserPrompt;
    std::vector<SkillDefinition> availableSkills;
    std::vector<MemoryHeader> relevantMemories;
    std::vector<ContextFile> projectContextFiles;
    std::optional<AgentDefinition> agentDefinition;
    bool coordinatorMode = false;
};
```

## EnvironmentInfo

```cpp
struct EnvironmentInfo {
    std::string osName;
    std::string shell;
    std::filesystem::path cwd;
    bool isGitRepo = false;
    std::string gitBranch;
    std::string date;
};
```

环境探测应尽量轻量，不要每轮执行大量命令。git 信息可以缓存，并在 cwd 变化时刷新。

## CLAUDE.md / AGENTS.md discovery

上游主要发现 CLAUDE.md。C++ 可以同时支持：

- `CLAUDE.md`
- `AGENTS.md`
- `.openharness/README.md` 可选

搜索策略：

1. 从 cwd 向上到 git root。
2. 收集每层的上下文文件。
3. 父目录内容先注入，近 cwd 的内容后注入。
4. 限制总字符数，防止 prompt 过长。

## Prompt 拼接顺序

建议顺序：

1. 基础 system prompt。
2. 当前环境。
3. 安全和权限模式。
4. 工具使用规则。
5. Skills 摘要。
6. Project context files。
7. Relevant memories。
8. Coordinator 或 agent-specific prompt。
9. Output style。

顺序不是随意的。基础规则和安全规则应靠前，当前项目上下文和 memory 靠后，便于模型看到最新任务相关内容。

## Skills 摘要

不要把所有 skill 全文都塞进 system prompt。通常只放摘要：

```text
Available skills:
- review: Review code for bugs and security issues.
- debug: Diagnose and fix bugs systematically.
```

当模型需要某个 skill 时，再调用 `skill` 工具加载全文。

## Memory 注入

只注入 relevant memories，不要注入全部 memory。格式示例：

```markdown
# Relevant Memories

## Build Tool

Use CMake as the build tool. Do not generate go.mod.
```

## Coordinator prompt

Coordinator mode 需要额外规则：

- worker 不共享完整上下文。
- 给 worker 的 prompt 必须自包含。
- 读多写少任务可以并发。
- 写文件任务要避免冲突。
- worker 完成后用 task notification 回报。

第一版没有 subagent 时可以不启用。

## Prompt 长度控制

需要设置预算：

- system prompt 最大字符数。
- project context 最大字符数。
- memory 最大条数和字符数。
- skill summary 最大数量。

如果 prompt 太长，优先删减：

1. 低相关 memory。
2. 过长 project context。
3. 低优先级 skill summary。

## 测试清单

- cwd 和 OS 信息正确注入。
- 权限模式变化会改变 prompt。
- CLAUDE.md 从父目录到子目录顺序正确。
- memory 只注入相关条目。
- prompt 长度超过预算时会截断。
- coordinator mode 注入 coordinator 规则。
