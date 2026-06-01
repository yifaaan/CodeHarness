#include "codeharness/core/json_parse.h"

#include <string>
#include <utility>

namespace codeharness
{

auto require_string(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name)
    -> Result<std::string>
{
    const std::string key{field_name};
    if (!input.contains(key) || !input[key].is_string())
    {
        return fail<std::string>(ErrorKind::InvalidArgument,
                                 std::string{tool_name} + " requires string field: " + key);
    }
    return input[key].get<std::string>();
}

auto require_int(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name) -> Result<int>
{
    const std::string key{field_name};
    if (!input.contains(key) || !input[key].is_number_integer())
    {
        return fail<int>(ErrorKind::InvalidArgument,
                         std::string{tool_name} + " requires integer field: " + key);
    }
    return input[key].get<int>();
}

auto require_bool(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name)
    -> Result<bool>
{
    const std::string key{field_name};
    if (!input.contains(key) || !input[key].is_boolean())
    {
        return fail<bool>(ErrorKind::InvalidArgument,
                          std::string{tool_name} + " requires boolean field: " + key);
    }
    return input[key].get<bool>();
}

auto optional_string(const nlohmann::json& input, std::string_view field_name) -> std::optional<std::string>
{
    const std::string key{field_name};
    if (input.contains(key) && input[key].is_string())
    {
        return input[key].get<std::string>();
    }
    return std::nullopt;
}

auto optional_int(const nlohmann::json& input, std::string_view field_name, int default_value,
                  std::string_view tool_name) -> Result<int>
{
    const std::string key{field_name};
    if (!input.contains(key))
    {
        return default_value;
    }
    if (!input[key].is_number_integer())
    {
        return fail<int>(ErrorKind::InvalidArgument,
                         std::string{tool_name} + ' ' + key + " must be an integer");
    }
    return input[key].get<int>();
}

auto optional_bool(const nlohmann::json& input, std::string_view field_name, bool default_value,
                   std::string_view tool_name) -> Result<bool>
{
    const std::string key{field_name};
    if (!input.contains(key))
    {
        return default_value;
    }
    if (!input[key].is_boolean())
    {
        return fail<bool>(ErrorKind::InvalidArgument,
                          std::string{tool_name} + ' ' + key + " must be a boolean");
    }
    return input[key].get<bool>();
}

} // namespace codeharness
