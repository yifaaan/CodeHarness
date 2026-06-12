#include "codeharness/tools/glob_tool.h"

#include <glob/glob.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness {

namespace {

struct GlobInput {
  std::string pattern;
  std::optional<std::string> path;
};

auto parse_glob_input(const nlohmann::json& input) -> absl::StatusOr<GlobInput> {
  GlobInput parsed;

  if (auto r = Assign(parsed.pattern, ReadJsonField<std::string>(input, "pattern", "glob")); !r.ok()) {
    return r;
  }
  if (auto r = Assign(parsed.path, ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(input, "path"));
      !r.ok()) {
    return r;
  }

  return parsed;
}

constexpr int MAX_RESULTS = 200;

}  // namespace

auto GlobTool::name() const -> std::string { return "glob"; }

auto GlobTool::description() const -> std::string {
  return "Search for files matching a glob pattern under the current workspace directory.";
}

auto GlobTool::is_read_only() const noexcept -> bool { return true; }

auto GlobTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return path_permission_target(request.parsed_input, "path");
}

auto GlobTool::execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> {
  auto parsed_input = parse_glob_input(request.parsed_input);
  if (!parsed_input.ok()) {
    return parsed_input.status();
  }

  auto search_root = context.cwd;

  if (parsed_input->path) {
    auto resolved = resolve_workspace_path(context.cwd, *parsed_input->path);
    if (!resolved.ok()) {
      return resolved.status();
    }

    if (!std::filesystem::is_directory(*resolved)) {
      return absl::InternalError("path is not a directory: " + resolved->string());
    }

    search_root = *resolved;
  }

  nlohmann::json result_json = nlohmann::json::array();
  std::set<std::string> emitted;

  auto append_matches = [&](const auto& matches) {
    for (size_t i = 0; i < matches.size() && result_json.size() < MAX_RESULTS; ++i) {
      std::error_code error;
      auto relative = std::filesystem::relative(matches[i], search_root, error);
      if (error) {
        continue;
      }

      auto result = relative.generic_string();
      if (!emitted.insert(result).second) {
        continue;
      }

      result_json.push_back(std::move(result));
    }
  };

  if (parsed_input->pattern.starts_with("**/")) {
    append_matches(glob::glob(search_root.string() + '/' + parsed_input->pattern.substr(3)));
  }

  append_matches(glob::rglob(search_root.string() + '/' + parsed_input->pattern));

  return ToolResponse{
      .tool_use_id = request.id,
      .content = result_json.dump(2),
      .is_error = false,
  };
}

}  // namespace codeharness
