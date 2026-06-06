//==============================================================================
// cli.cpp — CodeHarness CLI 入口
//
// 架构角色：应用入口
// 职责：解析命令行参数，初始化运行时，选择运行模式（交互式 /
//       非交互式 / backend-only），执行 prompt。
//
// 运行模式：
//   1. --version：打印版本号后退出
//   2. --backend-only：JSON Lines 协议模式，通过 stdin/stdout 与 UI 通信
//   3. --prompt "..."：非交互式模式，执行单条 prompt 后退出
//   4. 无参数（交互式）：打印帮助后退出（TUI 交互由前端实现）
//
// 启动流程：
//   1. 初始化日志（init_logger）
//   2. 切换工作目录（--cwd）
//   3. 创建 RuntimeBundle（组装所有子系统）
//   4. 根据 --backend-only 选择不同路由：
//      a. backend-only → BackendHost::run()
//      b. slash command → execute_slash_command()
//      c. 普通 prompt → run_prompt()
//   5. EngineEvent 通过 lambda 回调处理（流式输出到终端）
#include "codeharness/cli/cli.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/config/config_loader.h"
#include "codeharness/core/log.h"
#include "codeharness/engine/engine.h"
#include "codeharness/runtime/runtime.h"
#include "codeharness/tui/tui_app.h"
#include "codeharness/ui_backend/ui_backend.h"
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
    bool backend_only = false;
    std::string prompt;
    std::string cwd;
    int max_turns = 0;
    std::string provider_type;
    std::string model;
    std::string api_key;
    std::string base_url;
    std::string profile;

    app.add_flag("--version", show_version, "Print version and exit");
    app.add_flag("--backend-only", backend_only, "Run the backend-only JSON Lines protocol");
    app.add_option("-p,--prompt", prompt, "Prompt to run in non-interactive mode");
    app.add_option("--cwd", cwd, "Working directory");
    app.add_option("--max-turns", max_turns, "Maximum number of turns");
    app.add_option("--provider", provider_type, "Provider type: echo (default), openai, anthropic");
    app.add_option("--model", model, "Model name (provider default when omitted)");
    app.add_option("--api-key", api_key, "API key (defaults to env or credentials file)");
    app.add_option("--base-url", base_url, "API base URL");
    app.add_option("--profile", profile, "Configuration profile to use");

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

    // Load configuration via ConfigLoader (defaults → settings.json → env → CLI).
    config::ConfigLoader loader;
    auto settings = loader.load(config::CliOptions{
        .provider_type = provider_type,
        .model = model,
        .api_key = api_key,
        .base_url = base_url,
        .max_turns = max_turns,
        .cwd = !cwd.empty() ? std::filesystem::path{cwd} : std::filesystem::current_path(),
        .profile = profile,
    });
    if (!settings)
    {
        return fail<int>(ErrorKind::Config,
                         "configuration error: " + settings.error().message);
    }

    auto runtime_bundle = runtime::create_runtime_bundle(
        runtime::RuntimeBundleOptions{
            .cwd = settings->cwd,
            .memory_root = settings->memory_root,
            .permission_mode = settings->permission.mode,
            .load_default_user_plugins = true,
            .provider_config = ProviderConfig{
                .type = settings->provider_type,
                .model = settings->model,
                .api_key = settings->api_key,
                .base_url = settings->base_url,
            },
        });
    if (!runtime_bundle)
    {
        return nonstd::make_unexpected(runtime_bundle.error());
    }

    if (backend_only)
    {
        ui_backend::BackendHost host{**runtime_bundle, std::cin, std::cout, settings->max_turns};
        auto hosted = host.run();
        if (!hosted)
        {
            return nonstd::make_unexpected(hosted.error());
        }

        return 0;
    }

    if (prompt.empty())
    {
        return tui::run_tui(**runtime_bundle, settings->max_turns);
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

    auto result = (*runtime_bundle)->run_prompt(prompt, settings->max_turns, [&](const EngineEvent& event) {
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
