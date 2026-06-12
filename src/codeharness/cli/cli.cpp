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

#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <variant>

#include "codeharness/commands/command_registry.h"
#include "codeharness/config/config_manager.h"
#include "codeharness/config/provider_manager.h"
#include "codeharness/core/log.h"
#include "codeharness/core/strings.h"
#include "codeharness/engine/engine.h"
#include "codeharness/runtime/runtime.h"
#include "codeharness/tui/tui_app.h"
#include "codeharness/ui_backend/ui_backend.h"
#include "codeharness/version.h"

namespace codeharness {

namespace {

auto make_profile_description(const runtime::RuntimeModelProfile& profile) -> std::string {
  auto description = profile.provider_config.type.empty() ? std::string{"echo"} : profile.provider_config.type;
  if (!profile.provider_config.model.empty()) {
    description += " / " + profile.provider_config.model;
  }
  return description;
}

std::vector<runtime::RuntimeModelProfile> BuildModelProfilesFromConfig(const config::CodeHarnessConfig& config,
                                                                       const config::Credentials& credentials) {
  std::vector<runtime::RuntimeModelProfile> profiles;
  profiles.reserve(config.providers.size());

  for (const auto& [id, pc] : config.providers) {
    auto api_key = config::resolve_api_key(pc.api_key, pc.type, credentials);
    ::codeharness::ProviderConfig provider_cfg{
        .type = pc.type.empty() ? std::string{"echo"} : pc.type,
        .model = pc.type,
        .api_key = std::move(api_key),
        .base_url = pc.base_url,
    };
    auto description = provider_cfg.type;
    profiles.push_back(runtime::RuntimeModelProfile{
        .id = id,
        .label = id,
        .description = std::move(description),
        .provider_config = std::move(provider_cfg),
    });
  }

  return profiles;
}

// Build a provider config from CLI/env overrides when no config file exists.
::codeharness::ProviderConfig BuildCliProviderConfig(const std::string& provider_type, const std::string& model,
                                                     const std::string& api_key, const std::string& base_url) {
  ::codeharness::ProviderConfig cfg;
  cfg.type = provider_type.empty() ? std::string{"echo"} : provider_type;
  cfg.model = model;
  cfg.api_key = api_key;
  cfg.base_url = base_url;
  return cfg;
}

}  // namespace

auto run_cli(int argc, char** argv) -> absl::StatusOr<int> {
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

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (show_version) {
    std::cout << PROJECT_NAME << ' ' << VERSION << '\n';
    return 0;
  }

  if (!cwd.empty()) {
    std::error_code error;
    std::filesystem::current_path(cwd, error);

    if (error) {
      return absl::StatusOr<int>(absl::InternalError("failed to change cwd: " + error.message()));
    }
  }

  // Load configuration via ConfigManager (config.toml → env → CLI).
  config::ConfigManager config_mgr(config::config_dir());
  auto maybe_config = config_mgr.Load();
  config::CodeHarnessConfig harness_config;
  if (maybe_config.ok()) {
    harness_config = std::move(*maybe_config);
  } else {
    // Config file not found or parse error — use defaults.
    auto status = maybe_config.status();
    if (!absl::IsNotFound(status)) {
      spdlog::warn("config load warning: {}", status.message());
    }
    harness_config.config_dir = config::config_dir();
    harness_config.data_dir = config::data_dir();
  }

  // Apply CLI/env overrides.
  if (!provider_type.empty()) {
    harness_config.default_model = model;
    // Create an override provider entry.
    config::ProviderConfig cli_pc;
    cli_pc.type = provider_type;
    cli_pc.api_key = api_key;
    cli_pc.base_url = base_url;
    harness_config.providers["cli"] = std::move(cli_pc);
  }

  if (!model.empty()) {
    harness_config.default_model = model;
  }

  auto has_explicit_provider_config = !provider_type.empty() || !model.empty() || !base_url.empty();

  // Build permission settings.
  codeharness::PermissionSettings permission;
  if (plan_mode) {
    permission.mode = PermissionMode::Plan;
  } else {
    permission.mode = PermissionMode::Default;
  }

  // Load credentials and build model profiles.
  auto credentials = config::load_credentials(harness_config.config_dir);
  if (!credentials) {
    return absl::StatusOr<int>(absl::FailedPreconditionError("credentials error: " + credentials.status().message()));
  }

  auto model_profiles = BuildModelProfilesFromConfig(harness_config, *credentials);
  if (model_profiles.empty()) {
    // No providers configured — create a default echo provider.
    model_profiles.push_back(runtime::RuntimeModelProfile{
        .id = "default",
        .label = "Default",
        .description = "echo",
        .provider_config = BuildCliProviderConfig(provider_type, model, api_key, base_url),
    });
  }

  // Determine active provider config.
  ::codeharness::ProviderConfig active_provider;
  std::string active_profile_id;
  if (has_explicit_provider_config) {
    active_provider = BuildCliProviderConfig(provider_type, model, api_key, base_url);
  } else if (!model_profiles.empty()) {
    active_provider = model_profiles[0].provider_config;
    active_profile_id = model_profiles[0].id;
  }

  auto runtime_bundle = runtime::create_runtime_bundle(runtime::RuntimeBundleOptions{
      .cwd = std::filesystem::current_path(),
      .memory_root = config::memory_dir(),
      .permission = std::move(permission),
      .hooks = {},
      .load_default_user_plugins = true,
      .provider_config = std::move(active_provider),
      .model_profiles = std::move(model_profiles),
      .active_model_profile_id = active_profile_id,
  });
  if (!runtime_bundle) {
    return runtime_bundle.error();
  }

  if (backend_only) {
    ui_backend::BackendHost host{**runtime_bundle, std::cin, std::cout, max_turns > 0 ? max_turns : 200};
    auto hosted = host.run();
    if (!hosted) {
      return hosted.error();
    }

    return 0;
  }

  if (prompt.empty()) {
    auto model_list = [&runtime_bundle]() -> std::vector<tui::ModelOption> {
      std::vector<tui::ModelOption> options;
      const auto current = (*runtime_bundle)->current_model_profile();
      for (const auto& profile : (*runtime_bundle)->model_profiles()) {
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
        **runtime_bundle, max_turns > 0 ? max_turns : 200,
        tui::TuiDisplayConfig{
            .model = model.empty() ? harness_config.default_model : model,
            .provider_type = provider_type.empty() ? "openai" : provider_type,
            .version = std::string{VERSION},
            .directory = (!cwd.empty() ? std::filesystem::path{cwd} : std::filesystem::current_path()).string(),
            .skill_count = static_cast<int>((*runtime_bundle)->skills().list().size()),
        },
        tui::ModelListProvider{model_list},
        tui::ModelSelectCallback{[&](const tui::ModelOption& selected) -> absl::StatusOr<tui::ModelOption> {
          auto profile = (*runtime_bundle)->find_model_profile(selected.value);
          if (!profile) {
            return fail<tui::ModelOption>(absl::InvalidArgumentError, "unknown model profile: " + selected.value);
          }

          auto switched = (*runtime_bundle)->switch_model_profile(*profile);
          if (!switched) {
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

  auto max_turns_effective = max_turns > 0 ? max_turns : 200;

  if (!prompt.empty() && prompt.front() == '/') {
    // Plan mode toggle commands are handled directly (no engine needed).
    auto trimmed = std::string{codeharness::Trim(prompt)};
    if (trimmed == "/plan" || trimmed == "/plan on" || trimmed == "/plan enter") {
      (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Plan);
      std::cout << "Entered plan mode. Read-only tools only.\n";
      return 0;
    }
    if (trimmed == "/act" || trimmed == "/plan off" || trimmed == "/plan exit") {
      (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Default);
      std::cout << "Default mode. Mutating tools allowed with confirmation.\n";
      return 0;
    }
    if (trimmed == "/fullauto" || trimmed == "/full_auto" || trimmed == "/permissions full_auto") {
      (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::FullAuto);
      std::cout << "Full-auto mode. Mutating tools are allowed unless blocked by safety rules.\n";
      return 0;
    }
    if (trimmed == "/default" || trimmed == "/permissions default") {
      (*runtime_bundle)->set_permission_mode(codeharness::PermissionMode::Default);
      std::cout << "Default mode. Mutating tools allowed with confirmation.\n";
      return 0;
    }
    if (trimmed == "/mode" || trimmed == "/permissions") {
      std::cout << "Current permission mode: "
                << codeharness::permission_mode_label((*runtime_bundle)->permission_mode()) << '\n';
      return 0;
    }

    auto command_result = execute_slash_command((*runtime_bundle)->commands(), prompt);
    if (!command_result) {
      return command_result.error();
    }

    if (command_result->message) {
      std::cout << *command_result->message;
      if (!command_result->message->empty() && command_result->message->back() != '\n') {
        std::cout << '\n';
      }
    }

    // message-only commands such as /skills finish inside the command path.
    // Skill commands return submit_prompt, which is then handed to the
    // existing engine flow exactly like a normal non-slash prompt.
    if (!command_result->submit_prompt) {
      return 0;
    }

    if (command_result->submit_model) {
      auto profile = (*runtime_bundle)->find_model_profile(*command_result->submit_model);
      if (!profile) {
        return absl::StatusOr<int>(
            absl::InvalidArgumentError("unknown model profile: " + *command_result->submit_model));
      }
      auto switched = (*runtime_bundle)->switch_model_profile(*profile);
      if (!switched) {
        return switched.error();
      }
    }

    prompt = *command_result->submit_prompt;
  }

  bool printed_text = false;

  auto result = (*runtime_bundle)->run_prompt(prompt, max_turns_effective, [&](const EngineEvent& event) {
    if (auto delta = std::get_if<EngineAssistantTextDelta>(&event)) {
      std::cout << delta->text << std::flush;
      printed_text = true;
    }
  });

  if (!result.ok()) {
    return result.status();
  }

  if (printed_text) {
    std::cout << '\n';
  }

  return 0;
}

}  // namespace codeharness
