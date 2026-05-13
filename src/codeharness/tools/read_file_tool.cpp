#include "codeharness/tools/read_file_tool.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>

#include <fstream>
#include <iterator>

#include "absl/strings/str_cat.h"
#include "base.h"
#include "codeharness/logging.h"

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
        -> absl::StatusOr<std::string> {
        if (!input.contains("path") || !input["path"].is_string()) {
            CH_LOG_WARN("ReadFileTool::execute", "missing or invalid string field path");
            return absl::InvalidArgumentError("read_file requires a string field: path");
        }

        const auto requested_path = input["path"].get<std::string>();
        const auto full_path = ctx.cwd / requested_path;
        CH_LOG_DEBUG("ReadFileTool::execute", "requested_path={} full_path={}", requested_path,
                     full_path.string());

        std::ifstream file{full_path, std::ios::in};
        if (!file.is_open()) {
            CH_LOG_WARN("ReadFileTool::execute", "failed to open full_path={}",
                        full_path.string());
            return absl::NotFoundError(
                absl::StrCat("failed to open file: ", full_path.string()));
        }

        const auto output = std::string{std::istreambuf_iterator<char>{file},
                                        std::istreambuf_iterator<char>{}};
        CH_LOG_DEBUG("ReadFileTool::execute", "read full_path={} bytes={}", full_path.string(),
                     output.size());

        return output;
    }
}  // namespace codeharness::tools
