#include "codeharness/tools/edit_file_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "codeharness/logging.h"

namespace codeharness::tools {

    auto EditFileTool::name() const -> absl::string_view { return "edit_file"; }

    auto EditFileTool::description() const -> absl::string_view {
        return "Edit an existing file by replacing a string.";
    }

    auto EditFileTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"path",
                  {
                      {"type", "string"},
                      {"description", "File path relative to the current workspace."},
                  }},
                 {"old_str",
                  {
                      {"type", "string"},
                      {"description", "Existing text to replace."},
                  }},
                 {"new_str",
                  {
                      {"type", "string"},
                      {"description", "Replacement text."},
                  }},
                 {"replace_all",
                  {
                      {"type", "boolean"},
                      {"description", "Replace every occurrence instead of only the first."},
                      {"default", false},
                  }},
             }},
            {"required", {"path", "old_str", "new_str"}},
            {"additionalProperties", false},
        };
    }

    auto EditFileTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto EditFileTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto requested_path = input.at("path").get<std::string>();
        const auto old_text = input.at("old_str").get<std::string>();
        const auto new_text = input.at("new_str").get<std::string>();
        const auto replace_all = input.value("replace_all", false);

        if (old_text.empty()) {
            return absl::InvalidArgumentError("old_str must not be empty");
        }

        auto full_path = std::filesystem::path{requested_path};
        if (full_path.is_relative()) {
            full_path = ctx.cwd / full_path;
        }
        full_path = full_path.lexically_normal();

        CH_LOG_DEBUG("EditFileTool::execute", "path={} full_path={} replace_all={}", requested_path,
                     full_path.string(), replace_all);

        if (std::filesystem::is_directory(full_path)) {
            return absl::InvalidArgumentError(
                absl::StrCat("Cannot edit directory: ", full_path.string()));
        }

        std::ifstream input_file{full_path, std::ios::in | std::ios::binary};
        if (!input_file.is_open()) {
            return absl::NotFoundError(absl::StrCat("File not found: ", full_path.string()));
        }

        auto content = std::string{std::istreambuf_iterator<char>{input_file},
                                   std::istreambuf_iterator<char>{}};
        auto replaced_any = false;

        if (replace_all) {
            std::size_t search_from = 0;
            while (true) {
                const auto position = content.find(old_text, search_from);
                if (position == std::string::npos) {
                    break;
                }
                content.replace(position, old_text.size(), new_text);
                search_from = position + new_text.size();
                replaced_any = true;
            }
        } else {
            const auto position = content.find(old_text);
            if (position != std::string::npos) {
                content.replace(position, old_text.size(), new_text);
                replaced_any = true;
            }
        }

        if (!replaced_any) {
            return absl::NotFoundError("old_str was not found in the file");
        }

        std::ofstream output_file{full_path, std::ios::out | std::ios::binary | std::ios::trunc};
        if (!output_file.is_open()) {
            return absl::PermissionDeniedError(
                absl::StrCat("failed to open file for writing: ", full_path.string()));
        }

        output_file << content;
        output_file.close();
        if (output_file.fail()) {
            return absl::InternalError(absl::StrCat("failed to write file: ", full_path.string()));
        }

        CH_LOG_DEBUG("EditFileTool::execute", "updated full_path={} bytes={}", full_path.string(),
                     content.size());
        return absl::StrCat("Updated ", full_path.string());
    }

}  // namespace codeharness::tools
