#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <fmt/base.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "codeharness/app/runtime_bundle.h"
#include "codeharness/config/paths.h"
#include "codeharness/config/setting.h"
#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/logging.h"
#include "codeharness/permissions/models.h"
#include "codeharness/prompts/environment_info.h"
#include "codeharness/ui/stream_json_renderer.h"

namespace {

    using namespace codeharness;

    struct CliOptions {
        std::string prompt;
        std::string output_format = "text";
        std::string model = "gpt-5.5";
        std::string permission_mode = "default";

        std::string system_prompt;
        std::string append_system_prompt;
        std::string base_url;
        std::string api_key;
        std::string cwd;
        std::string settings_file;

        bool model_provided = false;
        bool verbose = false;
    };

    struct PrintModeResult {
        std::string text;
        std::optional<engine::ConversationMessage> message;
        engine::UsageSnapshot usage;
        std::vector<nlohmann::json> tool_events;
    };

    [[nodiscard]] auto default_log_file_path() -> std::filesystem::path {
        return config::paths::user_logs_root(/*create_if_missing=*/true) / "codeharness.log";
    }

    [[nodiscard]] auto build_system_prompt(const CliOptions& options,
                                           const std::filesystem::path& cwd) -> std::string {
        auto prompt = std::string{app::RuntimeBundle::default_system_prompt()};

        // 用cli参数替换默认system-prompt
        if (!options.system_prompt.empty()) {
            prompt = options.system_prompt;
        }

        const auto env_info = prompts::collect_environment(cwd);
        const auto env_block = prompts::format_environment_block(env_info);

        if (options.append_system_prompt.empty()) {
            return absl::StrCat(prompt, "\n\n", env_block);
        }

        return absl::StrCat(prompt, "\n\n", env_block, "\n\n", options.append_system_prompt);
    }

    [[nodiscard]] auto resolve_working_directory(const CliOptions& options)
        -> absl::StatusOr<std::filesystem::path> {
        namespace fs = std::filesystem;
        auto cwd = fs::current_path();

        if (!options.cwd.empty()) {
            cwd = options.cwd;
            if (cwd.is_relative()) {
                cwd = fs::current_path() / cwd;
            }
        }
        std::error_code ec;
        cwd = fs::weakly_canonical(cwd, ec);
        if (ec) {
            return absl::InvalidArgumentError(
                fmt::format("failed to resolve --cwd '{}': {}", options.cwd, ec.message()));
        }

        if (!fs::exists(cwd)) {
            return absl::InvalidArgumentError(
                fmt::format("working directory does not exist: {}", cwd.string()));
        }

        if (!fs::is_directory(cwd)) {
            return absl::InvalidArgumentError(
                fmt::format("working directory is not a directory: {}", cwd.string()));
        }

        return cwd;
    }

    [[nodiscard]] auto resolve_settings_file_path(const CliOptions& options)
        -> std::optional<std::filesystem::path> {
        if (options.settings_file.empty()) {
            return std::nullopt;
        }
        namespace fs = std::filesystem;
        auto path = fs::path{options.settings_file};
        if (path.is_relative()) {
            path = fs::current_path() / path;
        }

        std::error_code ec;
        path = fs::weakly_canonical(path, ec);
        if (ec) {
            path = fs::absolute(path);
        }

        return path;
    }

    [[nodiscard]] auto build_print_mode_json(const PrintModeResult& result) -> nlohmann::json {
        auto json = nlohmann::json{
            {"ok", true},
            {"text", result.text},
            {"usage", result.usage},
            {"tool_events", result.tool_events},
        };

        if (result.message) {
            json["message"] = *result.message;
        }
        return json;
    }

    int run_print_mode(const CliOptions& options) {
        CH_LOG_DEBUG("run_print_mode", "model_arg={} model_provided={}", options.model,
                     options.model_provided);

        auto overrides = config::SettingsOverrides{
            .permission_mode = permissions::parse_permission_mode(options.permission_mode),
        };

        if (options.model_provided) {
            overrides.model = options.model;
        }
        if (!options.base_url.empty()) {
            overrides.base_url = options.base_url;
        }
        if (!options.api_key.empty()) {
            overrides.api_key = options.api_key;
        }

        if (const auto settings_file = resolve_settings_file_path(options);
            settings_file.has_value()) {
            overrides.settings_file = *settings_file;
        }

        const auto settings = config::load_settings(overrides);
        if (!settings.ok()) {
            fmt::println(stderr, "Failed to load settings: {}", settings.status().message());
            return EXIT_FAILURE;
        }

        const auto cwd = resolve_working_directory(options);
        if (!cwd.ok()) {
            fmt::println(stderr, "Invalid working directory: {}", cwd.status().message());
            return EXIT_FAILURE;
        }

        auto system_prompt = build_system_prompt(options, *cwd);

        auto runtime = app::RuntimeBundle::create(*settings, *cwd, system_prompt);
        if (!runtime.ok()) {
            fmt::println(stderr, "Failed to initialize runtime: {}", runtime.status().message());
            return EXIT_FAILURE;
        }

        CH_LOG_DEBUG("run_print_mode",
                     "settings loaded model={} base_url={} permission_mode={} api_key_present={} "
                     "system_prompt_chars={} cwd={} settings_file={}",
                     settings->api.model, settings->api.base_url, options.permission_mode,
                     !settings->api.api_key.empty(), system_prompt.size(), cwd->string(),
                     options.settings_file.empty() ? "<default>" : options.settings_file);

        CH_LOG_DEBUG("run_print_mode", "registered_tools={} cwd={}",
                     runtime->tools().list_tools().size(), runtime->cwd().string());

        CH_LOG_DEBUG("run_print_mode", "submitting prompt_chars={} output_format={}",
                     options.prompt.size(), options.output_format);

        PrintModeResult result;
        const auto status =
            runtime->engine().submit_message(options.prompt, [&](const engine::StreamEvent& event) {
                if (options.output_format == "stream-json") {
                    fmt::println("{}", ui::to_stream_json(event).dump());
                    return;
                }
                if (auto delta = std::get_if<engine::AssistantTextDelta>(&event)) {
                    result.text += delta->text;
                    if (options.output_format == "text") {
                        fmt::print("{}", delta->text);
                    }
                    return;
                }
                if (auto complete = std::get_if<engine::AssistantTurnComplete>(&event)) {
                    result.message = complete->message;
                    result.usage = complete->usage;
                    return;
                }
                if (auto tool_use_start = std::get_if<engine::ToolExecutionStared>(&event)) {
                    result.tool_events.push_back(*tool_use_start);
                    return;
                }
                if (auto tool_execution_complete =
                        std::get_if<engine::ToolExecutionComplete>(&event)) {
                    result.tool_events.push_back(*tool_execution_complete);
                    return;
                }
            });
        if (!status.ok()) {
            fmt::println(stderr, "Request failed: {}", status.message());
            return EXIT_FAILURE;
        }
        if (options.output_format == "text" && !result.text.empty()) {
            fmt::print("\n");
        }

        if (options.output_format == "json") {
            fmt::println("{}", build_print_mode_json(result).dump(2));
        }

        CH_LOG_DEBUG("run_print_mode", "completed successfully");

        return EXIT_SUCCESS;
    }

}  // namespace

int main(int argc, char** argv) {
    CliOptions options;

    CLI::App app{"CodeHarness - C++23 learning harness for agent development"};
    app.option_defaults()->always_capture_default();

    app.add_option("-p,--print", options.prompt, "Run one non-interactive prompt and exit");
    app.add_option("--output-format", options.output_format,
                   "Output format: text, json, stream-json")
        ->check(CLI::IsMember({"text", "json", "stream-json"}));
    auto* model_option = app.add_option("-m,--model", options.model, "Model name or alias");
    app.add_option("--permission-mode", options.permission_mode,
                   "Permission mode: default, plan, full_auto")
        ->check(CLI::IsMember({"default", "plan", "full_auto"}));
    app.add_flag("-v,--verbose", options.verbose, "Enable debug logging");
    app.add_option("--base-url", options.base_url, "Override API base URL for this run");
    app.add_option("--api-key", options.api_key, "Override API key for this run");
    app.add_option("-s,--system-prompt", options.system_prompt,
                   "Replace the default system prompt");
    app.add_option("--append-system-prompt", options.append_system_prompt,
                   "Append extra text to the system prompt");
    app.add_option("--cwd", options.cwd, "Working directory for this run");
    app.add_option("--settings", options.settings_file, "Path to a JSON settings file");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    options.model_provided = model_option->count() > 0;
    const auto log_level = options.verbose ? spdlog::level::debug : spdlog::level::info;
    const auto log_path = default_log_file_path();
    if (const auto status = logging::initialize_default_logger(log_path, log_level); !status.ok()) {
        fmt::println(stderr, "Failed to initialize logging: {}", status.message());
        return EXIT_FAILURE;
    }
    CH_LOG_DEBUG("main",
                 "parsed cli options prompt_chars={} model={} output_format={} "
                 "permission_mode={} model_provided={}",
                 options.prompt.size(), options.model, options.output_format,
                 options.permission_mode, options.model_provided);

    if (not options.prompt.empty()) {
        return run_print_mode(options);
    }

    fmt::println("CodeHarness bootstrap is ready. Use -p \"hello\" to run print mode.");
    return EXIT_SUCCESS;
}
