#include "codeharness/tools/enter_plan_mode_tool.h"

#include <absl/strings/string_view.h>

#include <string>

#include "codeharness/tools/config_tool.h"

namespace codeharness::tools {

    auto EnterPlanModeTool::name() const -> absl::string_view { return "enter_plan_mode"; }

    auto EnterPlanModeTool::description() const -> absl::string_view {
        return "Switch permission mode to plan.";
    }

    auto EnterPlanModeTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"additionalProperties", false},
        };
    }

    auto EnterPlanModeTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto EnterPlanModeTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(input);

        auto config_tool = ConfigTool{};
        const auto result = config_tool.execute(
            nlohmann::json{
                {"action", "set"},
                {"key", "permission_mode"},
                {"value", "plan"},
            },
            ctx);
        if (!result.ok()) {
            return result.status();
        }
        return "Permission mode set to plan";
    }

}  // namespace codeharness::tools
