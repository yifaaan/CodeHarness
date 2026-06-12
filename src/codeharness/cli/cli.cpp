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
//   1. 初始化日志（InitLogger）
//   2. 切换工作目录（--cwd）
//   3. 创建 RuntimeBundle（组装所有子系统）
//   4. 根据 --backend-only 选择不同路由：
//      a. backend-only → BackendHost::run()
//      b. slash command → execute_slash_command()
//      c. 普通 prompt → run_prompt()
//   5. EngineEvent 通过 lambda 回调处理（流式输出到终端）
#include "codeharness/cli/cli.h"

#include "codeharness/core/strings.h"

#include "codeharness/commands/command_registry.h"
#include "codeharness/config/config_loader.h"
#include "codeharness/config/credentials.h"
#include "codeharness/core/log.h"
#include "codeharness/engine/engine.h"
#include "codeharness/runtime/runtime.h"
#include "codeharness/tui/tui_app.h"
#include "codeharness/ui_backend/ui_backend.h"
#include "codeharness/version.h"

#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <variant>

namespace codeharness
{

namespace
{

auto make_profile_description(const runtime::RuntimeModelProfile& profile) -> std::string
{
    auto description = profile.provider_config.type.empty() ? std::string{"echo"} : profile.provider_config.type;
    if (!profile.provider_config.model.empty())
    {
        description += " / " + profile.provider_config.model;
    }
    return description;
}

auto build_runtime_model_profiles(const config::Settings& settings) -> absl::StatusOr<std::vector<runtime::RuntimeModelProfile>>
{
    auto credentials = config::load_credentials(settings.config_dir);
    if(!credentials.ok())
    {
        return credentials.status();
    }

    std::vector<runtime::RuntimeModelProfile> profiles;
    profiles.reserve(settings.profiles.size());
    for (const auto& [id, profile] : settings.profiles)
    {
        ProviderConfig provider_config{
            .type = profile.provider_type.empty() ? std::string{"echo"} : profile.provider_type,
            .model = profile.model,
            .api_key = config::resolve_api_key(profile.auth_source, profile.provider_type, *credentials),
            .base_url = profile.base_url,
        };
        auto label = profile.label.empty() ? id : profile.label;
        auto description = provider_config.type;
        if (!provider_config.model.empty())
        {
            description += " / " + provider_config.model;
        }
        profiles.push_back(runtime::RuntimeModelProfile{
            .id = id,
            .label = std::move(label),
            .description = std::move(description),
            .provider_config = std::move(provider_config),
        });
    }

    return profiles;
}

} // namespace

auto run_cli(int argc, char** argv) -> absl::StatusOr<int>
{
    InitLogger();
    spdlog::info("codeharness starting ({} {})", PROJECT_NAME, VERSION);

    CLI::App app{"CodeHarness"};

    bool show_version = false;
    bool backend_only = false;
    bool plan_mode = false;
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
    app.add_flag("--plan", plan_mode, "Start in plan mode (read-only analysis)");
    app.add_option("-p,--prompt", prompt, "Prompt to run in non-interactive mode");
    app.add_option("--cwd", cwd, "Working directory");
    app.add_option("--max-turns", max_turns, "Maximum number of turns");
    app.add_option("--provider", provider_type, "Provider type: openai (default), anthropic, echo");
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
            return absl::StatusOr<int>(absl::InternalError("failed to change cwd: " + error.message()));
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
        return absl::StatusOr<int>(absl::FailedPreconditionError("configuration error: " + settings.error()).message);
    }

    auto permission = settings->permission;
    if (plan_mode)
    {
        permission.mode = PermissionMode::Plan;
    }

    auto model_profiles = build_runtime_model_profiles(*settings);
    if (!model_profiles)
    {
        return model_profiles.error();
    }
    const auto has_explicit_provider_config = !provider_type.empty() || !model.empty() || !base_url.empty();

    auto runtime_bundle = runtime::create_runtime_bundle(
        runtime::RuntimeBundleOptions{
            .cwd = settings->cwd,
            .memory_root = settings->memory_root,
            .permission = std::move(permission),
            .hooks = settings->hooks,
            .load_default_user_plugins = true,
            .provider_config = ProviderConfig{
                .type = settings->provider_type,
                .model = settings->model,
                .api_key = settings->api_key,
                .base_url = settings->base_url,
            },
            .model_profiles = *model_profiles,
            .active_model_profile_id = has_explicit_provider_config ? std::string{} : settings->active_profile,
        });
    if (!runtime_bundle)
    {
        return runtime_bundle.error();
    }

    if (backend_only)
    {
        ui_backend::BackendHost host{**runtime_bundle, std::cin, std::cout, settings->max_turns};
        auto hosted = host.run();
        if (!hosted)
        {
            return hosted.error();
        }

        return 0;
    }

    if (prompt.empty())
    {
        auto model_list = [&runtime_bundle]() -> std::vector<tui::ModelOption> {
            std::vector<tui::ModelOption> options;
            const auto current = (*runtime_bundle)->current_model_profile();
            for (const auto& profile : (*runtime_bundle)->model_profiles())
            {
                options.push_back(tui::ModelOption{
                    .value = profile.id,
                    .label = profile.label,
                    .description = make_profile_description(profile),
                    .is_current = profile.id == current.id,
                });
            }
            return options;
        };

        return tui::run_tui(
            **runtime_bundle,
            settings->max_turns,
            tui::TuiDisplayConfig{
                .model = settings->model,
                .provider_type = settings->provider_type,
                .version = std::string{VERSION},
                .directory = settings->cwd.string(),
                .skill_count = static_cast<int>((*runtime_bundle)->skills().list().size()),
            },
            tui::ModelListProvider{model_list},
            tui::ModelSelectCallback{[&](const tui::ModelOption& selected) -> absl::StatusOr<tui::ModelOption> {
                auto profile = (*runtime_bundle)->find_model_profile(selected.value);
                if (!profile)
                {
                    return fail<tui::ModelOption>(absl::InvalidArgumentError , "unknown model profile: " + selected.value);
                }

                auto switched = (*runtime_bundle)->switch_model_profile(*profile);
                if (!switched)
                {
                    return switched.error();
                }

                return tui::ModelOption{
                    .value = switched->provider_config.model,
                    .label = switched->label,
                    .description = make_profile_description(*switched),
                    .is_current = true,
                };
            }});
    }

    if (!prompt.empty() && prompt.front() == '/')
    {
        // Plan mode toggle commands are handled directly (no engine needed).
        auto trimmed = std::string{codeharness::Trim(prompt)};
        if (trimmed == "/plan" || trimmed == "/plan on" || trimmed == "/plan enter")
        {
            (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Plan);
            std::cout << "Entered plan mode. Read-only tools only.\n";
            return 0;
        }
        if (trimmed == "/act" || trimmed == "/plan off" || trimmed == "/plan exit")
        {
            (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Default);
            std::cout << "Default mode. Mutating tools allowed with confirmation.\n";
            return 0;
        }
        if (trimmed == "/fullauto" || trimmed == "/full_auto" || trimmed == "/permissions full_auto")
        {
            (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::FullAuto);
            std::cout << "Full-auto mode. Mutating tools are allowed unless blocked by safety rules.\n";
            return 0;
        }
        if (trimmed == "/default" || trimmed == "/permissions default")
        {
            (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Default);
            std::cout << "Default mode. Mutating tools allowed with confirmation.\n";
            return 0;
        }
        if (trimmed == "/mode" || trimmed == "/permissions")
        {
            std::cout << "Current permission mode: "
                      << codeharness::permission_mode_label((*runtime_bundle)->permission_mode()) << '\n';
            return 0;
        }

        auto command_result = execute_slash_command((*runtime_bundle)->commands(), prompt);
        if (!command_result)
        {
            return command_result.error();
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

        if (command_result->submit_model)
        {
            auto profile = (*runtime_bundle)->find_model_profile(*command_result->submit_model);
            if (!profile)
            {
                return absl::StatusOr<int>(absl::InvalidArgumentError("unknown model profile: " + *command_result->submit_model));
            }
            auto switched = (*runtime_bundle)->switch_model_profile(*profile);
            if (!switched)
            {
                return switched.error();
            }
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

    if(!result.ok())
    {
        return result.status();
    }

    if (printed_text)
    {
        std::cout << '\n';
    }

    return 0;
}

} // namespace codeharness
