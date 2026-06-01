#include "codeharness/tools/permission_target.h"

#include <filesystem>
#include <utility>

namespace codeharness
{

namespace
{

auto string_field(const nlohmann::json& input, std::string_view field_name) -> std::optional<std::string>
{
    const std::string key{field_name};
    if (input.contains(key) && input[key].is_string())
    {
        return input[key].get<std::string>();
    }
    return std::nullopt;
}

} // namespace

auto path_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    if (auto value = string_field(input, field_name))
    {
        target.path = std::filesystem::path{*value};
    }

    return target;
}

auto command_permission_target(const nlohmann::json& input, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    if (auto value = string_field(input, field_name))
    {
        target.command = std::move(*value);
    }

    return target;
}

} // namespace codeharness
