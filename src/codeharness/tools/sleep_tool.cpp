#include "codeharness/tools/sleep_tool.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>
#include <fmt/core.h>

#include <chrono>
#include <thread>

namespace codeharness::tools {

    auto SleepTool::name() const -> absl::string_view { return "sleep"; }

    auto SleepTool::description() const -> absl::string_view {
        return "Sleep for a short duration.";
    }

    auto SleepTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"seconds",
                  {{"type", "number"}, {"default", 1.0}, {"minimum", 0.0}, {"maximum", 30.0}}},
             }},
            {"additionalProperties", false},
        };
    }

    auto SleepTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto SleepTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto seconds = input.value("seconds", 1.0);
        if (seconds < 0.0 || seconds > 30.0) {
            return absl::InvalidArgumentError("seconds must be between 0 and 30");
        }

        std::this_thread::sleep_for(std::chrono::duration<double>{seconds});
        return fmt::format("Slept for {} seconds", seconds);
    }

}  // namespace codeharness::tools
