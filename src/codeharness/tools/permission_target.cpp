#include "codeharness/tools/permission_target.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace codeharness
{

namespace
{

auto string_field(std::string_view input_json, std::string_view field_name) -> std::optional<std::string>
{
    try
    {
        const auto input = nlohmann::json::parse(input_json.begin(), input_json.end());
        const auto key = std::string{field_name};

        if (input.contains(key) && input[key].is_string())
        {
            return input[key].get<std::string>();
        }
    }
    catch (const nlohmann::json::parse_error&)
    {
        // 权限目标提取不负责参数诊断；具体工具会在 execute 里返回解析错误。
    }

    return std::nullopt;
}

} // namespace

auto path_permission_target(std::string_view input_json, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    if (auto value = string_field(input_json, field_name))
    {
        target.path = std::filesystem::path{*value};
    }

    return target;
}

auto command_permission_target(std::string_view input_json, std::string_view field_name) -> PermissionTarget
{
    PermissionTarget target;

    if (auto value = string_field(input_json, field_name))
    {
        target.command = std::move(*value);
    }

    return target;
}

} // namespace codeharness
