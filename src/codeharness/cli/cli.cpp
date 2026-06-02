#include "codeharness/cli/cli.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/log.h"
#include "codeharness/engine/engine.h"
#include "codeharness/memory/memory_store.h"
#include "codeharness/prompts/system_prompt.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/tools/bash_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/skill_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/version.h"

#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>
#include <nonstd/expected.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

namespace codeharness
{

auto load_relevant_memories_for_prompt(
    const memory::MemoryStore& store, std::string_view prompt, std::size_t max_results)
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

auto run_cli(int argc, char** argv) -> Result<int>
{
    init_logger();
    spdlog::info("codeharness starting ({} {})", PROJECT_NAME, VERSION);

    CLI::App app{"CodeHarness"};

    bool show_version = false;
    std::string prompt;
    std::string cwd;
    int max_turns = 10;

    app.add_flag("--version", show_version, "Print version and exit");
    app.add_option("-p,--prompt", prompt, "Prompt to run in non-interactive mode");
    app.add_option("--cwd", cwd, "Working directory");
    app.add_option("--max-turns", max_turns, "Maximum number of turns");

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& error)
    {
        return app.exit(error);
    }

    if (show_version)
    {
        std::cout << PROJECT_NAME << ' ' << VERSION << '\n';
        return 0;
    }

    if (!cwd.empty())
    {
        std::error_code error;
        std::filesystem::current_path(cwd, error);

        if (error)
        {
            return fail<int>(ErrorKind::Io, "failed to change cwd: " + error.message());
        }
    }

    if (prompt.empty())
    {
        std::cout << app.help() << '\n';
        return 0;
    }

    SkillLoadOptions skill_options;
    skill_options.plugin_options.load_default_user_plugins = true;

    auto loaded_skills = load_skill_registry_with_plugins(std::filesystem::current_path(), std::move(skill_options));
    if (!loaded_skills)
    {
        return nonstd::make_unexpected(loaded_skills.error());
    }

    auto memory_store = memory::MemoryStore::for_project(std::filesystem::current_path());
    if (!memory_store)
    {
        return nonstd::make_unexpected(memory_store.error());
    }

    auto command_options = BuiltinCommandRegistryOptions{
        .memory_store = &*memory_store,
        .plugins = std::span<const LoadedPlugin>{loaded_skills->plugins},
    };
    auto commands = build_builtin_command_registry(loaded_skills->registry, command_options);

    if (!prompt.empty() && prompt.front() == '/')
    {
        auto command_result = execute_slash_command(commands, prompt);
        if (!command_result)
        {
            return nonstd::make_unexpected(command_result.error());
        }

        if (command_result->message)
        {
            std::cout << *command_result->message;
            if (!command_result->message->empty() && command_result->message->back() != '\n')
            {
                std::cout << '\n';
            }
        }

        // message-only commands such as /skills finish inside the command path.
        // Skill commands return submit_prompt, which is then handed to the
        // existing engine flow exactly like a normal non-slash prompt.
        if (!command_result->submit_prompt)
        {
            return 0;
        }

        prompt = *command_result->submit_prompt;
    }

    auto project_context_files = load_project_context_files(std::filesystem::current_path());
    if (!project_context_files)
    {
        return nonstd::make_unexpected(project_context_files.error());
    }

    PromptBuildRequest prompt_request;
    prompt_request.cwd = std::filesystem::current_path();
    prompt_request.latest_user_prompt = prompt;
    prompt_request.available_skills = loaded_skills->registry.list();
    prompt_request.available_commands = commands.list();
    prompt_request.project_context_files = std::move(*project_context_files);
    prompt_request.permission_mode = PermissionMode::Default;
    auto relevant_memories = load_relevant_memories_for_prompt(*memory_store, prompt);
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

    ToolRegistry tools;
    tools.add(std::make_unique<ReadFileTool>());
    tools.add(std::make_unique<EditFileTool>());
    tools.add(std::make_unique<GlobTool>());
    tools.add(std::make_unique<GrepTool>());
    tools.add(std::make_unique<BashTool>());
    tools.add(std::make_unique<SkillTool>(loaded_skills->registry));

    EchoProvider provider;
    Engine engine{provider, tools};

    RunRequest request;
    request.prompt = prompt;
    request.system_prompt = *system_prompt;
    request.options.max_turns = max_turns;

    bool printed_text = false;

    auto result = engine.run_streaming(request, [&](const EngineEvent& event) {
        if (auto delta = std::get_if<EngineAssistantTextDelta>(&event))
        {
            std::cout << delta->text << std::flush;
            printed_text = true;
        }
    });

    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    if (printed_text)
    {
        std::cout << '\n';
    }

    return 0;
}

} // namespace codeharness
