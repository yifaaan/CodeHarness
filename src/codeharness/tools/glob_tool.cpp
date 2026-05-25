#include "codeharness/tools/glob_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <glob/glob.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <sstream>

#include "absl/status/statusor.h"
#include "codeharness/logging.h"
#include "codeharness/tools/base.h"
#include "nlohmann/json_fwd.hpp"

namespace codeharness::tools {
    auto GlobTool::name() const -> absl::string_view { return "glob"; }

    auto GlobTool::description() const -> absl::string_view {
        return "List paths matching a glob pattern in the workspace.";
    }

    auto GlobTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"pattern",
                  {{"type", "string"},
                   {"description", "Glob pattern, for example *.cpp or **/*.h."}}},
                 {"root",
                  {{"type", "string"}, {"description", "Optional search root relative to cwd."}}},
                 {"limit",
                  {{"type", "integer"}, {"default", 200}, {"minimum", 1}, {"maximum", 5000}}},
             }},
            {"required", {"pattern"}},
            {"additionalProperties", false},
        };
    }

    auto GlobTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto GlobTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        auto pattern = input.at("pattern").get<std::string>();
        const auto root_arg = input.value("root", std::string{});
        const auto limit = input.value("limit", 200);

        if (pattern.empty()) {
            return absl::InvalidArgumentError("pattern must not be empty");
        }
        if (limit < 1 || limit > 5000) {
            return absl::InvalidArgumentError("limit must be between 1 and 5000");
        }

        std::replace(pattern.begin(), pattern.end(), '\\', '/');

        auto root = root_arg.empty() ? ctx.cwd : std::filesystem::path{root_arg};
        if (root.is_relative()) {
            root = ctx.cwd / root;
        }
        root = root.lexically_normal();

        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root)) {
            return std::string{"(no matched)"};
        }

        const auto full_pattern_path = (root / std::filesystem::path{pattern}).lexically_normal();
        const auto full_pattern = full_pattern_path.generic_string();
        CH_LOG_DEBUG("GlobTool::execute", "root={} pattern={} full_pattern={}", root.string(),
                     pattern, full_pattern);

        auto raw_matches = std::vector<std::filesystem::path>{};
        if (pattern.find("**") != std::string::npos) {
            raw_matches = glob::rglob(full_pattern);
        } else {
            raw_matches = glob::glob(full_pattern);
        }

        auto matches = std::vector<std::string>{};
        for (const auto& path : raw_matches) {
            auto relative = std::filesystem::relative(path, root, ec);
            if (ec) {
                ec.clear();
                continue;
            }

            const auto text = relative.generic_string();
            if (!text.empty() && text != ".") {
                matches.push_back(text);
            }
        }

        std::sort(matches.begin(), matches.end());

        if (matches.empty()) {
            return std::string{"(no matches)"};
        }

        auto output = std::ostringstream{};
        const auto count = std::min<std::size_t>(matches.size(), static_cast<std::size_t>(limit));
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                output << '\n';
            }
            output << matches[i];
        }

        return output.str();
    }
}  // namespace codeharness::tools