#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace codeharness
{

struct PermissionTarget
{
    std::optional<std::filesystem::path> path;
    std::optional<std::string> command;
};

// 从工具 JSON 输入里提取路径型权限目标。
auto path_permission_target(std::string_view input_json, std::string_view field_name) -> PermissionTarget;

// 从工具 JSON 输入里提取命令型权限目标，主要给 bash 使用。
auto command_permission_target(std::string_view input_json, std::string_view field_name) -> PermissionTarget;

} // namespace codeharness
