# Prompts C++20 实现参考

Prompts 模块的 C++20 实现已完成，代码见 `src/codeharness/prompts/`（6 个文件）。

## 已实现的能力

| 能力 | 代码位置 |
| --- | --- |
| `SystemPromptBuilder` | `prompts/system_prompt.h/.cpp` — `build_system_prompt(request)` |
| `EnvironmentDetector` | `prompts/system_prompt.h` — `detect_environment(cwd)` → `EnvironmentInfo{osName, shell, cwd, isGitRepo, gitBranch, date}` |
| `ProjectContextLoader` | `prompts/project_context.h/.cpp` — `load_project_context_files(cwd)` |
| AGENTS.md / CLAUDE.md 搜索 | 从 cwd 向上到 git root，收集每层上下文文件，父目录先注入，近 cwd 后注入 |
| `PromptBuildRequest` | settings、cwd、latestUserPrompt、availableSkills、relevantMemories、projectContextFiles、agentDefinition、coordinatorMode |
| Memory 注入 | 只注入 `relevantMemories`（相关性搜索前 5 条），不全部注入 |
| Skills 摘要 | 列出 skills 名和简介，全文通过 `skill` 工具按需加载 |
| Prompt 长度控制 | system prompt、project context、memory 各自有字符数预算，超出优先裁剪低相关内容 |

## Prompt 拼接顺序

```
1. 基础 system prompt（身份/行为规则）
2. 当前环境（OS、shell、cwd、git 信息）
3. 安全和权限模式
4. 工具使用规则
5. Skills 摘要
6. Project context files（AGENTS.md / CLAUDE.md）
7. Relevant memories
8. Coordinator / agent-specific prompt
9. Output style
```

## 测试覆盖

`tests/prompt_tests.cpp` 覆盖：cwd/OS 信息注入、权限模式变化、AGENTS.md 父→子顺序、memory 相关注入、prompt 长度截断
