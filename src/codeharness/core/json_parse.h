#pragma once

#include "codeharness/core/error.h"
#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace codeharness
{

enum class JsonFieldMode
{
    required,
    optional_with_default,
    optional_if_valid,
};

// 解析工具 JSON 字段的统一 helper。
// 错误信息格式：`"<tool> requires <kind> field: <field_name>"` 或
// `"<tool> <field_name> must be a <kind>"`，由调用方提供 tool_name。
template <typename T, JsonFieldMode mode = JsonFieldMode::required>
auto read_json_field(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name = {},
                     T default_value = {})
    -> std::conditional_t<mode == JsonFieldMode::optional_if_valid, Result<std::optional<T>>, Result<T>>
{
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, int> || std::is_same_v<T, bool>,
                  "unsupported JSON field type");

    auto required_name = std::string_view{};
    auto optional_name = std::string_view{};
    auto matches_type = false;
    const auto key = std::string{field_name};

    if constexpr (std::is_same_v<T, std::string>)
    {
        required_name = "string";
        optional_name = "a string";
    }
    else if constexpr (std::is_same_v<T, int>)
    {
        required_name = "integer";
        optional_name = "an integer";
    }
    else
    {
        required_name = "boolean";
        optional_name = "a boolean";
    }

    using ResultValue = std::conditional_t<mode == JsonFieldMode::optional_if_valid, std::optional<T>, T>;

    if (!input.contains(key))
    {
        if constexpr (mode == JsonFieldMode::required)
        {
            return fail<ResultValue>(ErrorKind::InvalidArgument,
                                     std::string{tool_name} + " requires " + std::string{required_name} +
                                         " field: " + key);
        }
        else if constexpr (mode == JsonFieldMode::optional_if_valid)
        {
            return std::optional<T>{};
        }
        else
        {
            return default_value;
        }
    }

    const auto& value = input.at(key);

    if constexpr (std::is_same_v<T, std::string>)
    {
        matches_type = value.is_string();
    }
    else if constexpr (std::is_same_v<T, int>)
    {
        matches_type = value.is_number_integer();
    }
    else
    {
        matches_type = value.is_boolean();
    }

    if (!matches_type)
    {
        if constexpr (mode == JsonFieldMode::optional_if_valid)
        {
            return std::optional<T>{};
        }
        else if constexpr (mode == JsonFieldMode::required)
        {
            return fail<ResultValue>(ErrorKind::InvalidArgument,
                                     std::string{tool_name} + " requires " + std::string{required_name} +
                                         " field: " + key);
        }
        else
        {
            return fail<ResultValue>(ErrorKind::InvalidArgument,
                                     std::string{tool_name} + ' ' + key + " must be " +
                                         std::string{optional_name});
        }
    }

    if constexpr (mode == JsonFieldMode::optional_if_valid)
    {
        return std::optional<T>{value.template get<T>()};
    }
    else
    {
        return value.template get<T>();
    }
}

// 严格 optional 字段 helper:
//   - 字段缺失:返回 nullopt
//   - 字段存在:复用 read_json_field<T> 的类型检查和错误消息
template <typename T>
auto read_optional_json_field(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name = {})
    -> Result<std::optional<T>>
{
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, int> || std::is_same_v<T, bool>,
                  "unsupported JSON field type");

    if (!input.contains(std::string{field_name}))
    {
        return std::optional<T>{};
    }

    auto value = read_json_field<T>(input, field_name, tool_name);
    if (!value)
    {
        return nonstd::make_unexpected(value.error());
    }

    return std::optional<T>{std::move(*value)};
}

} // namespace codeharness
