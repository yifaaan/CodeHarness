#include "codeharness/cli/cli.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/core/log.h"
#include "codeharness/engine/engine.h"
#include "codeharness/runtime/runtime.h"
#include "codeharness/version.h"

#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>
#include <nonstd/expected.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <variant>

namespace codeharness
{

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

    auto runtime_bundle = runtime::create_runtime_bundle(
        runtime::RuntimeBundleOptions{
            .cwd = std::filesystem::current_path(),
            .permission_mode = PermissionMode::Default,
            .load_default_user_plugins = true,
        });
    if (!runtime_bundle)
    {
        return nonstd::make_unexpected(runtime_bundle.error());
    }

    if (!prompt.empty() && prompt.front() == '/')
    {
        auto command_result = execute_slash_command((*runtime_bundle)->commands(), prompt);
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

    bool printed_text = false;

    auto result = (*runtime_bundle)->run_prompt(prompt, max_turns, [&](const EngineEvent& event) {
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
