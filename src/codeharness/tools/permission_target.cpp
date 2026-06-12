#include "codeharness/tools/permission_target.h"

#include <filesystem>
#include <utility>

#include "codeharness/core/json_parse.h"

namespace codeharness {

auto path_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget {
  PermissionTarget target;

  auto value = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(input, field_name);
  if (value.ok() && *value) {
    target.path = std::filesystem::path{**value};
  }

  return target;
}

auto command_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget {
  PermissionTarget target;

  auto value = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(input, field_name);
  if (value.ok() && *value) {
    target.command = std::move(**value);
  }

  return target;
}

}  // namespace codeharness
