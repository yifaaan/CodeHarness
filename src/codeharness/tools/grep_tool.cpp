#include "codeharness/tools/grep_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <glob/glob.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "codeharness/logging.h"

namespace codeharness::tools {

    auto GrepTool::name() const -> absl::string_view { return "grep"; }

    auto GrepTool::description() const -> absl::string_view {
        return "Search file contents with a regular expression.";
    }

    auto GrepTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"pattern",
                  {{"type", "string"}, {"description", "Regular expression to search for."}}},
                 {"root",
                  {{"type", "string"}, {"description", "Optional search root relative to cwd."}}},
                 {"file_glob",
                  {{"type", "string"},
                   {"description", "Glob pattern for files to search."},
                   {"default", "**/*"}}},
                 {"case_sensitive",
                  {{"type", "boolean"},
                   {"description", "Whether matching is case-sensitive."},
                   {"default", true}}},
                 {"limit",
                  {{"type", "integer"}, {"default", 200}, {"minimum", 1}, {"maximum", 2000}}},
             }},
            {"required", {"pattern"}},
            {"additionalProperties", false},
        };
    }

    auto GrepTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto GrepTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto pattern_text = input.at("pattern").get<std::string>();
        const auto root_arg = input.value("root", std::string{});
        auto file_glob = input.value("file_glob", std::string{"**/*"});
        const auto case_sensitive = input.value("case_sensitive", true);
        const auto limit = input.value("limit", 200);

        if (file_glob.empty()) {
            return absl::InvalidArgumentError("file_glob must not be empty");
        }
        if (limit < 1 || limit > 2000) {
            return absl::InvalidArgumentError("limit must be between 1 and 2000");
        }

        std::replace(file_glob.begin(), file_glob.end(), '\\', '/');

        auto root = root_arg.empty() ? ctx.cwd : std::filesystem::path{root_arg};
        if (root.is_relative()) {
            root = ctx.cwd / root;
        }
        root = root.lexically_normal();

        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
            return std::string{"(no matches)"};
        }

        auto flags = std::regex_constants::ECMAScript;
        if (!case_sensitive) {
            flags |= std::regex_constants::icase;
        }

        auto matcher = std::regex{};
        try {
            matcher = std::regex{pattern_text, flags};
        } catch (const std::regex_error& error) {
            return absl::InvalidArgumentError(
                absl::StrCat("invalid regular expression: ", error.what()));
        }

        const auto full_pattern_path = (root / std::filesystem::path{file_glob}).lexically_normal();
        const auto full_pattern = full_pattern_path.generic_string();

        CH_LOG_DEBUG("GrepTool::execute",
                     "root={} pattern={} file_glob={} full_pattern={} case_sensitive={} limit={}",
                     root.string(), pattern_text, file_glob, full_pattern, case_sensitive, limit);

        auto candidate_paths = std::vector<std::filesystem::path>{};
        try {
            if (file_glob.find("**") != std::string::npos) {
                candidate_paths = glob::rglob(full_pattern);
            } else {
                candidate_paths = glob::glob(full_pattern);
            }
        } catch (const std::exception& error) {
            return absl::InvalidArgumentError(
                absl::StrCat("invalid file_glob pattern: ", error.what()));
        }

        std::sort(candidate_paths.begin(), candidate_paths.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return lhs.generic_string() < rhs.generic_string();
                  });

        auto matches = std::vector<std::string>{};
        for (const auto& path : candidate_paths) {
            if (matches.size() >= static_cast<std::size_t>(limit)) {
                break;
            }

            if (!std::filesystem::is_regular_file(path, ec)) {
                ec.clear();
                continue;
            }

            auto file = std::ifstream{path, std::ios::in | std::ios::binary};
            if (!file.is_open()) {
                continue;
            }

            const auto content =
                std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
            if (content.find('\0') != std::string::npos) {
                continue;
            }

            auto relative = std::filesystem::relative(path, root, ec);
            if (ec) {
                ec.clear();
                continue;
            }

            auto lines = std::istringstream{content};
            auto line = std::string{};
            std::size_t line_no = 1;
            while (matches.size() < static_cast<std::size_t>(limit) && std::getline(lines, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (std::regex_search(line, matcher)) {
                    matches.push_back(
                        absl::StrCat(relative.generic_string(), ":", line_no, ":", line));
                }
                ++line_no;
            }
        }

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
