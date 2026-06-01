#pragma once

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/result.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/prompts/project_context.h"
#include "codeharness/skills/skill.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace codeharness
{

// 当前运行环境快照:由 detect_environment 得到,
// system prompt 的 "# Environment" 段,
// 让模型知道它是在哪个 OS / shell / cwd / 日期下执行任务,
// 以及是否在 git 仓库里、当前分支是什么。
struct EnvironmentInfo
{
    std::string os_name;                       // Windows / macOS / Linux / Unknown
    std::string shell;                         // $SHELL 或 $COMSPEC 的 basename
    std::filesystem::path cwd;                 // 规范化后的绝对路径
    std::string date;                          // YYYY-MM-DD
    bool is_git_repo = false;
    std::optional<std::string> git_branch;
};

// "与当前任务相关"memory;
// title 用作 prompt 里的二级标题,content 裸放
struct RelevantMemory
{
    std::string title;
    std::string content;
};

//   - 行为边界 / 环境 / 权限 / 能力  → 进 system prompt
//   - 最近用户消息                   → 单独作为对话回合追加,这里只预留位
struct PromptBuildRequest
{
    std::filesystem::path cwd;                        // 跑 detect_environment
    std::optional<std::string> latest_user_prompt;
    std::vector<SkillDefinition> available_skills;    // 来自 SkillRegistry::list()
    std::vector<SlashCommand> available_commands;     // 来自 CommandRegistry::list()
    std::vector<ContextFile> project_context_files;   // 来自 ProjectContextLoader(AGENTS.md / CLAUDE.md)
    std::vector<RelevantMemory> relevant_memories;
    PermissionMode permission_mode = PermissionMode::Default;  // 当前会话的权限模式
};

// 探测环境
auto detect_environment(const std::filesystem::path& cwd) -> Result<EnvironmentInfo>;

// 把 PromptBuildRequest 拼装成一段完整的 system prompt 文本。
// 内部按"基础人格 → 环境 → 权限 → 能力 → 项目上下文 → 记忆"的固定顺序拼接
class SystemPromptBuilder
{
public:
    auto build(const PromptBuildRequest& request) const -> Result<std::string>;
};

} // namespace codeharness
