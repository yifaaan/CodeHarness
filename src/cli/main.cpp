#include <absl/status/status.h>
#include <fmt/base.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/api/openai_client.h"
#include "codeharness/config/setting.h"
#include "codeharness/engine/query_engine.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/permissions/checker.h"
#include "codeharness/permissions/models.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/ui/stream_json_renderer.h"

namespace {

    using namespace codeharness;
    struct CliOptions {
        std::string prompt;
        std::string output_format = "text";
        std::string model = "gpt-5.5";
        std::string permission_mode = "default";
        bool model_provided = false;
        bool verbose = false;
    };

    int run_print_mode(const CliOptions& options) {
        spdlog::set_level(options.verbose ? spdlog::level::debug : spdlog::level::info);
        spdlog::debug("running print mode with model_arg={} model_provided={}", options.model,
                      options.model_provided);

        auto overrides = config::SettingsOverrides{
            .permission_mode = permissions::parse_permission_mode(options.permission_mode),
        };
        if (options.model_provided) {
            overrides.model = options.model;
        }
        const auto settings = config::load_settings(overrides);
        if (!settings.ok()) {
            fmt::println(stderr, "Failed to load settings: {}", settings.status().message());
            return EXIT_FAILURE;
        }
        spdlog::debug("settings loaded: model={} base_url={} permission_mode={} api_key_present={}",
                      settings->api.model, settings->api.base_url, options.permission_mode,
                      !settings->api.api_key.empty());

        auto api = api::OpenAIClient{
            api::OpenAIClientOptions{
                .api_key = settings->api.api_key,
                .base_url = settings->api.base_url,
                .timeout = settings->api.timeout,
            },
        };

        auto tools = tools::ToolRegistry{};
        if (const auto status = tools.register_tool(std::make_unique<tools::ReadFileTool>());
            !status.ok()) {
            fmt::println(stderr, "Failed to register tool: {}", status.message());
            return EXIT_FAILURE;
        }
        const auto permissions = permissions::PermissionChecker{settings->permissions};
        spdlog::debug("registered {} tool(s); cwd={}", tools.list_tools().size(),
                      std::filesystem::current_path().string());

        auto engine = engine::QueryEngine{
            api,
            tools,
            permissions,
            std::filesystem::current_path(),
            settings->api.model,
            "You are CodeHarness, a  concise coding assistant.",
        };

        spdlog::debug("submitting prompt: chars={} output_format={}", options.prompt.size(),
                      options.output_format);
        const auto status = engine.submit_message(options.prompt, [&](const engine::StreamEvent& event) {
            // using StreamEvent = std::variant<AssistantTextDelta, AssistantTurnComplete,
            // ToolExecutionStared,  ToolExecutionComplete>;
            if (options.output_format == "stream-json") {
                fmt::println("{}", ui::to_stream_json(event).dump());
                return;
            }
            if (auto delta = std::get_if<engine::AssistantTextDelta>(&event)) {
                fmt::print("{}", delta->text);
            }
            // if (auto complete = std::get_if<engine::AssistantTurnComplete>(&event)) {
            //     fmt::println("\nTurn completed:\n{}", complete->message.text());
            // }
            // if (auto tool_use_start = std::get_if<engine::ToolExecutionStared>(&event)) {
            //     fmt::println("Tool use: {}", tool_use_start->tool_name);
            // }
            // if (auto tool_execution_complete =
            // std::get_if<engine::ToolExecutionComplete>(&event)) {
            //     fmt::println("Tool result: {}", tool_execution_complete->output);
            // }
        });
        if (!status.ok()) {
            fmt::println(stderr, "Request failed: {}", status.message());
            return EXIT_FAILURE;
        }
        spdlog::debug("print mode completed");

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

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    options.model_provided = model_option->count() > 0;
    spdlog::set_level(options.verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::debug(
        "parsed cli options: prompt_chars={} model={} output_format={} "
        "permission_mode={} model_provided={}",
        options.prompt.size(), options.model, options.output_format, options.permission_mode,
        options.model_provided);

    if (not options.prompt.empty()) {
        return run_print_mode(options);
    }

    fmt::println("CodeHarness bootstrap is ready. Use -p \"hello\" to run print mode.");
    return EXIT_SUCCESS;
}
