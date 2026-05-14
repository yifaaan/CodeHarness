#include "codeharness/tools/write_file_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <fstream>

#include "codeharness/logging.h"

namespace codeharness::tools {

    auto WriteFileTool::name() const -> absl::string_view { return "write_file"; }

    auto WriteFileTool::description() const -> absl::string_view {
        return "Create or overwrite a text file in the workspace.";
    }

    auto WriteFileTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"path",
                  {
                      {"type", "string"},
                      {"description", "File path relative to the current workspace."},
                  }},
                 {"content",
                  {
                      {"type", "string"},
                      {"description", "Full file contents to write."},
                  }},
                 {"create_directories",
                  {
                      {"type", "boolean"},
                      {"description", "Create parent directories if they don't exist."},
                      {"default", true},
                  }},
             }},
            {"required", {"path", "content"}},
            {"additionalProperties", false},
        };
    }

    auto WriteFileTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto WriteFileTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto requested_path = input["path"].get<std::string>();
        const auto content = input["content"].get<std::string>();
        const auto create_dirs = input.value("create_directories", true);

        const auto full_path = (ctx.cwd / requested_path).lexically_normal();
        CH_LOG_DEBUG("WriteFileTool::execute", "path={} full_path={} bytes={}",
                     requested_path, full_path.string(), content.size());

        if (create_dirs) {
            std::error_code ec;
            std::filesystem::create_directories(full_path.parent_path(), ec);
            if (ec) {
                CH_LOG_WARN("WriteFileTool::execute", "failed to create dirs: {}",
                            ec.message());
                return absl::InternalError(
                    absl::StrCat("failed to create directories: ", ec.message()));
            }
        }

        std::ofstream file{full_path, std::ios::out | std::ios::trunc};
        if (!file.is_open()) {
            CH_LOG_WARN("WriteFileTool::execute", "failed to open for write: {}",
                        full_path.string());
            return absl::PermissionDeniedError(
                absl::StrCat("failed to open file for writing: ", full_path.string()));
        }

        file << content;
        file.close();

        if (file.fail()) {
            CH_LOG_WARN("WriteFileTool::execute", "write error: {}", full_path.string());
            return absl::InternalError(
                absl::StrCat("failed to write file: ", full_path.string()));
        }

        CH_LOG_DEBUG("WriteFileTool::execute", "wrote full_path={} bytes={}",
                     full_path.string(), content.size());
        return absl::StrCat("Wrote ", full_path.string());
    }

}  // namespace codeharness::tools
