#include "codeharness/cli/cli.h"

#include "codeharness/core/message.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/version.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace codeharness
{

auto run_cli(int argc, char **argv) -> Result<int>
{
    CLI::App app{"CodeHarness"};

    bool show_version = false;
    std::string prompt;
    std::string cwd;

    app.add_flag("--version", show_version, "Print version and exit");
    app.add_option("-p,--prompt", prompt, "Prompt to run in non-interactive mode");
    app.add_option("--cwd", cwd, "Working directory");

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

    EchoProvider provider;
    std::vector<Message> messages;
    messages.push_back(make_text_message(Role::User, prompt));

    auto response = provider.generate(std::span<const Message>(messages));
    if (!response)
    {
        return nonstd::make_unexpected(response.error());
    }

    std::cout << collect_text(*response) << '\n';
    return 0;
}

} // namespace codeharness