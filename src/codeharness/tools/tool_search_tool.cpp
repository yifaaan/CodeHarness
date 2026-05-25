#include "codeharness/tools/tool_search_tool.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "codeharness/logging.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness::tools {
namespace {

    [[nodiscard]] auto lower_ascii(std::string text) -> std::string {
        std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    }

}  // namespace

    auto ToolSearchTool::name() const -> absl::string_view { return "tool_search"; }

    auto ToolSearchTool::description() const -> absl::string_view {
        return "Search the available tool list by name or description.";
    }

    auto ToolSearchTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"query",
                  {{"type", "string"},
                   {"description", "Substring to search in tool names and descriptions."}}},
             }},
            {"required", {"query"}},
            {"additionalProperties", false},
        };
    }

    auto ToolSearchTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto ToolSearchTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        if (ctx.tool_registry == nullptr) {
            return absl::FailedPreconditionError("Tool registry context not available");
        }

        const auto query = lower_ascii(input.at("query").get<std::string>());
        CH_LOG_DEBUG("ToolSearchTool::execute", "query={}", query);

        auto matches = std::vector<std::string>{};
        for (const auto* tool : ctx.tool_registry->list_tools()) {
            const auto tool_name = std::string{tool->name()};
            const auto tool_description = std::string{tool->description()};
            const auto searchable = lower_ascii(tool_name + " " + tool_description);
            if (searchable.find(query) != std::string::npos) {
                matches.push_back(tool_name + ": " + tool_description);
            }
        }

        std::sort(matches.begin(), matches.end());

        if (matches.empty()) {
            return std::string{"(no matches)"};
        }

        auto output = std::ostringstream{};
        for (std::size_t i = 0; i < matches.size(); ++i) {
            if (i != 0) {
                output << '\n';
            }
            output << matches[i];
        }

        return output.str();
    }

}  // namespace codeharness::tools
