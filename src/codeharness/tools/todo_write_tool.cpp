#include "codeharness/tools/todo_write_tool.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "codeharness/logging.h"

namespace codeharness::tools {
namespace {

    [[nodiscard]] auto rtrim_ascii(std::string text) -> std::string {
        const auto last = text.find_last_not_of(" \t\r\n\f\v");
        if (last == std::string::npos) {
            return {};
        }
        text.erase(last + 1);
        return text;
    }

}  // namespace

    auto TodoWriteTool::name() const -> absl::string_view { return "todo_write"; }

    auto TodoWriteTool::description() const -> absl::string_view {
        return "Append a TODO item to a markdown checklist file.";
    }

    auto TodoWriteTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"item", {{"type", "string"}, {"description", "TODO item text."}}},
                 {"checked", {{"type", "boolean"}, {"default", false}}},
                 {"path", {{"type", "string"}, {"default", "TODO.md"}}},
             }},
            {"required", {"item"}},
            {"additionalProperties", false},
        };
    }

    auto TodoWriteTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return false;
    }

    auto TodoWriteTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto item = input.at("item").get<std::string>();
        const auto checked = input.value("checked", false);
        const auto requested_path = input.value("path", std::string{"TODO.md"});

        if (item.empty()) {
            return absl::InvalidArgumentError("item must not be empty");
        }
        if (requested_path.empty()) {
            return absl::InvalidArgumentError("path must not be empty");
        }

        auto full_path = std::filesystem::path{requested_path};
        if (full_path.is_relative()) {
            full_path = ctx.cwd / full_path;
        }
        full_path = full_path.lexically_normal();

        CH_LOG_DEBUG("TodoWriteTool::execute", "path={} full_path={} checked={}", requested_path,
                     full_path.string(), checked);

        if (std::filesystem::is_directory(full_path)) {
            return absl::InvalidArgumentError(
                absl::StrCat("Cannot write TODO to directory: ", full_path.string()));
        }

        std::error_code ec;
        std::filesystem::create_directories(full_path.parent_path(), ec);
        if (ec) {
            return absl::InternalError(
                absl::StrCat("failed to create directories: ", ec.message()));
        }

        auto existing = std::string{"# TODO\n"};
        if (std::filesystem::exists(full_path, ec)) {
            std::ifstream input_file{full_path, std::ios::in | std::ios::binary};
            if (!input_file.is_open()) {
                return absl::PermissionDeniedError(
                    absl::StrCat("failed to open TODO file: ", full_path.string()));
            }
            existing = std::string{std::istreambuf_iterator<char>{input_file},
                                   std::istreambuf_iterator<char>{}};
        }

        const auto prefix = checked ? "- [x]" : "- [ ]";
        const auto updated = absl::StrCat(rtrim_ascii(std::move(existing)), "\n", prefix, " ",
                                          item, "\n");

        std::ofstream output_file{full_path, std::ios::out | std::ios::binary | std::ios::trunc};
        if (!output_file.is_open()) {
            return absl::PermissionDeniedError(
                absl::StrCat("failed to open TODO file for writing: ", full_path.string()));
        }

        output_file << updated;
        output_file.close();
        if (output_file.fail()) {
            return absl::InternalError(
                absl::StrCat("failed to write TODO file: ", full_path.string()));
        }

        return absl::StrCat("Updated ", full_path.string());
    }

}  // namespace codeharness::tools
