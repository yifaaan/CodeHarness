//==============================================================================
// runtime.cpp — RuntimeBundle 实现
//
// 这部分的核心逻辑在 RuntimeBundle 的构造函数和 create_runtime_bundle
// 工厂函数中。构造时的关键细节：
//
//   1. create_command_registry — 构建内置命令（slash commands）
//      这些命令包括 /help, /skills, /model, /clear 等。
//      SkillRegistry 中的每个 skill 也可能注册自己的命令。
//
//   2. create_tool_registry — 构建 LLM 可调用工具
//      这是 LLM agent 的核心能力来源：
//      - 文件操作：ReadFileTool, EditFileTool, GlobTool, GrepTool
//      - 命令执行：BashTool
//      - 技能调用：SkillTool
//      - 任务管理：task_create, task_list, task_get, task_output, task_stop
//      - 子 agent：agent tool（通过 register_task_tools 注入 spawn_handler）
//      - 消息：mailbox tools（register_mailbox_tools）
//
//   3. load_relevant_memories_for_prompt — 记忆检索
//      在构建 prompt 时，从 memory store 中检索与当前提示词相关的记忆。
//      这些记忆以 <relevant-memories> XML 标签嵌入系统提示词。
//
//   4. build_run_request — 构造完整请求
//      合并系统提示词 + 项目上下文文件 + 相关记忆 + 可用 skill 列表
//      + 可用命令列表 → 形成完整的 RunRequest，交给 Engine 执行。
//==============================================================================

#include "codeharness/runtime/runtime.h"

#include "codeharness/mailbox/mailbox_tools.h"
#include "codeharness/prompts/project_context.h"
#include "codeharness/prompts/system_prompt.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/tasks/task_tools.h"
#include "codeharness/tools/bash_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/skill_tool.h"

#include <nonstd/expected.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace codeharness::runtime
{

namespace
{

auto load_relevant_memories_for_prompt(const memory::MemoryStore& store,
                                       std::string_view prompt,
                                       std::size_t max_results = 5)
    -> Result<std::vector<RelevantMemory>>
{
    auto entries = store.search(prompt, max_results);
    if (!entries)
    {
        return nonstd::make_unexpected(entries.error());
    }

    std::vector<RelevantMemory> memories;
    memories.resize(entries->size());
    std::ranges::transform(*entries, memories.begin(), [](const auto& entry) {
        return RelevantMemory{
            .title = entry.header.title,
            .content = entry.body,
        };
    });

    return memories;
}

auto create_command_registry(SkillRegistry& skills,
                             memory::MemoryStore& memory_store,
                             std::span<const LoadedPlugin> plugins) -> CommandRegistry
{
    return build_builtin_command_registry(
        skills,
        BuiltinCommandRegistryOptions{
            .memory_store = &memory_store,
            .plugins = plugins,
        });
}

auto create_tool_registry(const SkillRegistry& skills, coordinator::CoordinatorRuntime& coordinator_runtime) -> ToolRegistry
{
    ToolRegistry tools;
    tools.add(std::make_unique<ReadFileTool>());
    tools.add(std::make_unique<EditFileTool>());
    tools.add(std::make_unique<GlobTool>());
    tools.add(std::make_unique<GrepTool>());
    tools.add(std::make_unique<BashTool>());
    tools.add(std::make_unique<SkillTool>(skills));
    tasks::register_task_tools(tools, coordinator_runtime.task_manager(), coordinator_runtime.spawn_handler());
    mailbox::register_mailbox_tools(tools, coordinator_runtime.mailbox(), &coordinator_runtime.task_manager());
    return tools;
}

} // namespace

RuntimeBundle::RuntimeBundle(std::filesystem::path cwd,
                             PermissionMode permission_mode,
                             SkillRegistryLoadResult loaded_skills,
                             memory::MemoryStore memory_store,
                             std::unique_ptr<coordinator::CoordinatorRuntime> coordinator_runtime) :
    cwd_{std::move(cwd)},
    permission_mode_{permission_mode},
    loaded_skills_{std::move(loaded_skills)},
    memory_store_{std::move(memory_store)},
    commands_{create_command_registry(loaded_skills_.registry, memory_store_, loaded_skills_.plugins)},
    coordinator_runtime_{std::move(coordinator_runtime)},
    tools_{create_tool_registry(loaded_skills_.registry, *coordinator_runtime_)},
    permissions_{PermissionSettings{.mode = permission_mode_}},
    provider_{},
    engine_{provider_, tools_, &permissions_}
{
}

auto RuntimeBundle::cwd() const noexcept -> const std::filesystem::path&
{
    return cwd_;
}

auto RuntimeBundle::permission_mode() const noexcept -> PermissionMode
{
    return permission_mode_;
}

auto RuntimeBundle::skills() const noexcept -> const SkillRegistry&
{
    return loaded_skills_.registry;
}

auto RuntimeBundle::plugins() const noexcept -> const std::vector<LoadedPlugin>&
{
    return loaded_skills_.plugins;
}

auto RuntimeBundle::memory_store() noexcept -> memory::MemoryStore&
{
    return memory_store_;
}

auto RuntimeBundle::memory_store() const noexcept -> const memory::MemoryStore&
{
    return memory_store_;
}

auto RuntimeBundle::commands() const noexcept -> const CommandRegistry&
{
    return commands_;
}

auto RuntimeBundle::tools() const noexcept -> const ToolRegistry&
{
    return tools_;
}

auto RuntimeBundle::coordinator_runtime() noexcept -> coordinator::CoordinatorRuntime&
{
    return *coordinator_runtime_;
}

auto RuntimeBundle::coordinator_runtime() const noexcept -> const coordinator::CoordinatorRuntime&
{
    return *coordinator_runtime_;
}

auto RuntimeBundle::build_run_request(std::string_view prompt, int max_turns) -> Result<RunRequest>
{
    auto project_context_files = load_project_context_files(cwd_);
    if (!project_context_files)
    {
        return nonstd::make_unexpected(project_context_files.error());
    }

    PromptBuildRequest prompt_request;
    prompt_request.cwd = cwd_;
    prompt_request.latest_user_prompt = std::string{prompt};
    prompt_request.available_skills = loaded_skills_.registry.list();
    prompt_request.available_commands = commands_.list();
    prompt_request.project_context_files = std::move(*project_context_files);
    prompt_request.permission_mode = permission_mode_;

    auto relevant_memories = load_relevant_memories_for_prompt(memory_store_, prompt);
    if (!relevant_memories)
    {
        return nonstd::make_unexpected(relevant_memories.error());
    }
    prompt_request.relevant_memories = std::move(*relevant_memories);

    auto system_prompt = SystemPromptBuilder{}.build(prompt_request);
    if (!system_prompt)
    {
        return nonstd::make_unexpected(system_prompt.error());
    }

    return RunRequest{
        .prompt = std::string{prompt},
        .system_prompt = std::move(*system_prompt),
        .options = EngineOptions{.max_turns = max_turns},
    };
}

auto RuntimeBundle::run_prompt(std::string_view prompt, int max_turns, const EngineEventSink& sink) -> Result<RunResult>
{
    auto request = build_run_request(prompt, max_turns);
    if (!request)
    {
        return nonstd::make_unexpected(request.error());
    }

    return engine_.run_streaming(*request, sink);
}

auto create_memory_store(const std::filesystem::path& cwd, const std::filesystem::path& memory_root)
    -> Result<memory::MemoryStore>
{
    if (memory_root.empty())
    {
        return memory::MemoryStore::for_project(cwd);
    }

    return memory::MemoryStore::for_project(cwd, memory_root);
}

auto create_runtime_bundle(RuntimeBundleOptions options) -> Result<std::unique_ptr<RuntimeBundle>>
{
    if (options.cwd.empty())
    {
        options.cwd = std::filesystem::current_path();
    }

    SkillLoadOptions skill_options;
    skill_options.plugin_options.load_default_user_plugins = options.load_default_user_plugins;

    auto loaded_skills = load_skill_registry_with_plugins(options.cwd, std::move(skill_options));
    if (!loaded_skills)
    {
        return nonstd::make_unexpected(loaded_skills.error());
    }

    auto memory_store = create_memory_store(options.cwd, options.memory_root);
    if (!memory_store)
    {
        return nonstd::make_unexpected(memory_store.error());
    }

    auto coordinator_runtime = coordinator::create_default_runtime(options.cwd);
    if (!coordinator_runtime)
    {
        return nonstd::make_unexpected(coordinator_runtime.error());
    }

    return std::make_unique<RuntimeBundle>(std::move(options.cwd),
                                           options.permission_mode,
                                           std::move(*loaded_skills),
                                           std::move(*memory_store),
                                           std::move(*coordinator_runtime));
}

} // namespace codeharness::runtime
