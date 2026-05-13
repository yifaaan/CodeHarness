#include <fmt/base.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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

namespace {

    using namespace codeharness;
    struct CliOptions {
        std::string prompt;
        std::string output_format = "text";
        std::string model = "gpt-5.5";
        std::string permission_mode = "default";
        bool verbose = false;
    };

    int run_print_mode(const CliOptions& options) {
        spdlog::set_level(options.verbose ? spdlog::level::debug : spdlog::level::info);
        spdlog::debug("running print mode with model={}", options.model);

        const auto settings = config::load_settings(config::SettingsOverrides{
            .model = options.model,
            .permission_mode = permissions::parse_permission_mode(options.permission_mode)});

        auto api = api::OpenAIClient{
            api::OpenAIClientOptions{
                .api_key = settings.api.api_key,
                .base_url = settings.api.base_url,
                .timeout = settings.api.timeout,
            },
        };

        auto tools = tools::ToolRegistry{};
        tools.register_tool(std::make_unique<tools::ReadFileTool>());
        const auto permissions = permissions::PermissionChecker{settings.permissions};

        auto engine = engine::QueryEngine{
            api,
            tools,
            permissions,
            std::filesystem::current_path(),
            settings.api.model,
            "You are CodeHarness, a  concise coding assistant.",
        };

        engine.submit_message(options.prompt, [](const engine::StreamEvent& event) {
            // using StreamEvent = std::variant<AssistantTextDelta, AssistantTurnComplete,
            // ToolExecutionStared,  ToolExecutionComplete>;
            if (auto delta = std::get_if<engine::AssistantTextDelta>(&event)) {
                fmt::print("{}", delta->text);
            }
            if (auto complete = std::get_if<engine::AssistantTurnComplete>(&event)) {
                fmt::println("\nTurn completed:\n{}", complete->message.text());
            }
            if (auto tool_use_start = std::get_if<engine::ToolExecutionStared>(&event)) {
                fmt::println("Tool use: {}", tool_use_start->tool_name);
            }
            if (auto tool_execution_complete = std::get_if<engine::ToolExecutionComplete>(&event)) {
                fmt::println("Tool result: {}", tool_execution_complete->output);
            }
        });
        const auto text =
            fmt::format("CodeHarness bootstrap is ready. prompt=\"{}\" model={} permission={}",
                        options.prompt, options.model, options.permission_mode);

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
    app.add_option("-m,--model", options.model, "Model name or alias");
    app.add_option("--permission-mode", options.permission_mode,
                   "Permission mode: default, plan, full_auto")
        ->check(CLI::IsMember({"default", "plan", "full_auto"}));
    app.add_flag("-v,--verbose", options.verbose, "Enable debug logging");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    if (not options.prompt.empty()) {
        return run_print_mode(options);
    }

    std::cout << "CodeHarness bootstrap is ready. Use -p \"hello\" to run print mode.\n";
    return EXIT_SUCCESS;
}
