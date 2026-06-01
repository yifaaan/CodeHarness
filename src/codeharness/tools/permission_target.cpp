#include "codeharness/tools/permission_target.h"

#include "codeharness/core/json_parse.h"

#include <filesystem>
#include <utility>

namespace codeharness
{

auto path_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    auto value = read_json_field<std::string, JsonFieldMode::optional_if_valid>(input, field_name);
    if (value && *value)
    {
        target.path = std::filesystem::path{**value};
    }

    return target;
}

auto command_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    auto value = read_json_field<std::string, JsonFieldMode::optional_if_valid>(input, field_name);
    if (value && *value)
    {
        target.command = std::move(**value);
    }

    return target;
}

} // namespace codeharness
