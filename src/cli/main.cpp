#include <cstdlib>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {

struct CliOptions {
    std::string prompt;
    std::string output_format = "text";
    std::string model = "fake-model";
    std::string permission_mode = "default";
    bool verbose = false;
};

int run_print_mode(const CliOptions& options) {
    spdlog::set_level(options.verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::debug("running print mode with model={}", options.model);

    const auto text = fmt::format(
        "CodeHarness bootstrap is ready. prompt=\"{}\" model={} permission={}",
        options.prompt,
        options.model,
        options.permission_mode);

    if (options.output_format == "json") {
        const nlohmann::json result{
            {"type", "result"},
            {"text", text},
            {"model", options.model},
            {"permission_mode", options.permission_mode},
        };
        std::cout << result.dump() << '\n';
        return EXIT_SUCCESS;
    }

    if (options.output_format == "stream-json") {
        const nlohmann::json delta{{"type", "assistant_delta"}, {"text", text}};
        const nlohmann::json complete{{"type", "assistant_complete"}, {"text", text}};
        std::cout << delta.dump() << '\n' << complete.dump() << '\n';
        return EXIT_SUCCESS;
    }

    std::cout << text << '\n';
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions options;

    CLI::App app{"CodeHarness - C++23 learning harness for agent development"};
    app.option_defaults()->always_capture_default();

    app.add_option("-p,--print", options.prompt, "Run one non-interactive prompt and exit");
    app.add_option("--output-format", options.output_format, "Output format: text, json, stream-json")
        ->check(CLI::IsMember({"text", "json", "stream-json"}));
    app.add_option("-m,--model", options.model, "Model name or alias");
    app.add_option("--permission-mode", options.permission_mode, "Permission mode: default, plan, full_auto")
        ->check(CLI::IsMember({"default", "plan", "full_auto"}));
    app.add_flag("-v,--verbose", options.verbose, "Enable debug logging");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }

    if (!options.prompt.empty()) {
        return run_print_mode(options);
    }

    std::cout << "CodeHarness bootstrap is ready. Use -p \"hello\" to run print mode.\n";
    return EXIT_SUCCESS;
}

