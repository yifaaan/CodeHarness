#include "codeharness/tools/read_file_tool.h"

#include <absl/strings/string_view.h>

#include <fstream>

#include "absl/strings/str_cat.h"
#include "base.h"

namespace codeharness::tools {

    auto ReadFileTool::name() const -> absl::string_view { return "read_file"; }

    auto ReadFileTool::description() const -> absl::string_view {
        return "Read a UTF-8 text file from the workspace.";
    }

    auto ReadFileTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"path",
                  {
                      {"type", "string"},
                      {"description", "File path relative to the current workspace."},
                  }},
             }},
            {"required", {"path"}},
            {"additionalProperties", false},
        };
    }

    auto ReadFileTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto ReadFileTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> ToolResult {
        if (!input.contains("path") || !input["path"].is_string()) {
            return ToolResult{
                .output = "read_file requires a string field: path",
                .is_error = true,
            };
        }

        const auto requested_path = input["path"].get<std::string>();
        const auto full_path = ctx.cwd / requested_path;

        std::ifstream file{full_path, std::ios::in};
        if (!file.is_open()) {
            return ToolResult{
                .output = absl::StrCat("Failed to open file: ", full_path.string()),
                .is_error = true,
            };
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();

        return ToolResult{
            .output = buffer.str(),
            .is_error = false,
        };
    }
}  // namespace codeharness::tools