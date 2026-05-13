#include "codeharness/tools/read_file_tool.h"

#include <absl/strings/string_view.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <iterator>

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
            spdlog::warn("read_file: missing or invalid path field");
            return ToolResult{
                .output = "read_file requires a string field: path",
                .is_error = true,
            };
        }

        const auto requested_path = input["path"].get<std::string>();
        const auto full_path = ctx.cwd / requested_path;
        spdlog::debug("read_file: opening path={}", full_path.string());

        std::ifstream file{full_path, std::ios::in};
        if (!file.is_open()) {
            spdlog::warn("read_file: failed to open path={}", full_path.string());
            return ToolResult{
                .output = absl::StrCat("Failed to open file: ", full_path.string()),
                .is_error = true,
            };
        }

        const auto output = std::string{std::istreambuf_iterator<char>{file},
                                        std::istreambuf_iterator<char>{}};
        spdlog::debug("read_file: read path={} bytes={}", full_path.string(), output.size());

        return ToolResult{
            .output = output,
            .is_error = false,
        };
    }
}  // namespace codeharness::tools
