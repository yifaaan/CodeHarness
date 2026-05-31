#include "codeharness/cli/cli.h"

#include "codeharness/engine/engine.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/version.h"

#include <CLI/CLI.hpp>
#include <nonstd/expected.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <variant>

namespace codeharness
{

auto run_cli(int argc, char **argv) -> Result<int>
{
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

    ToolRegistry tools;
    auto _ = tools.add(std::make_unique<ReadFileTool>());

    EchoProvider provider;
    Engine engine{provider, tools};

    RunRequest request;
    request.prompt = prompt;
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
