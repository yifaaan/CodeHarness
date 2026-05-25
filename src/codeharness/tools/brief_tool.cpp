#include "codeharness/tools/brief_tool.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>

#include <string>

namespace codeharness::tools {
namespace {

    [[nodiscard]] auto trim_ascii(std::string text) -> std::string {
        const auto first = text.find_first_not_of(" \t\r\n\f\v");
        if (first == std::string::npos) {
            return {};
        }

        const auto last = text.find_last_not_of(" \t\r\n\f\v");
        return text.substr(first, last - first + 1);
    }

    [[nodiscard]] auto rtrim_ascii(std::string text) -> std::string {
        const auto last = text.find_last_not_of(" \t\r\n\f\v");
        if (last == std::string::npos) {
            return {};
        }
        text.erase(last + 1);
        return text;
    }

}  // namespace

    auto BriefTool::name() const -> absl::string_view { return "brief"; }

    auto BriefTool::description() const -> absl::string_view {
        return "Shorten a piece of text for compact display.";
    }

    auto BriefTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"text", {{"type", "string"}, {"description", "Text to shorten."}}},
                 {"max_chars",
                  {{"type", "integer"}, {"default", 200}, {"minimum", 20}, {"maximum", 2000}}},
             }},
            {"required", {"text"}},
            {"additionalProperties", false},
        };
    }

    auto BriefTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto BriefTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto max_chars = input.value("max_chars", 200);
        if (max_chars < 20 || max_chars > 2000) {
            return absl::InvalidArgumentError("max_chars must be between 20 and 2000");
        }

        const auto text = trim_ascii(input.at("text").get<std::string>());
        if (text.size() <= static_cast<std::size_t>(max_chars)) {
            return text;
        }

        auto shortened = text.substr(0, static_cast<std::size_t>(max_chars));
        shortened = rtrim_ascii(std::move(shortened));
        shortened += "...";
        return shortened;
    }

}  // namespace codeharness::tools
