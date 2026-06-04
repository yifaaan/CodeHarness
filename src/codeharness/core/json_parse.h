#pragma once

#include "codeharness/core/error.h"
#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace codeharness
{

enum class JsonFieldMode
{
    required,
    optional_with_default,
    optional_if_valid,
};

template <typename T>
inline constexpr bool is_supported_json_field_v =
    std::is_same_v<T, std::string> || std::is_same_v<T, int> || std::is_same_v<T, bool> || std::is_same_v<T, std::vector<std::string>> || std::is_same_v<T, std::map<std::string, std::string>>;

// 解析工具 JSON 字段的统一 helper。
// 错误信息格式：`"<tool> requires <kind> field: <field_name>"` 或
// `"<tool> <field_name> must be a <kind>"`，由调用方提供 tool_name。
template <typename T, JsonFieldMode mode = JsonFieldMode::required>
auto read_json_field(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name = {}, T default_value = {}, ErrorKind error_kind = ErrorKind::InvalidArgument)
    -> std::conditional_t<mode == JsonFieldMode::optional_if_valid, Result<std::optional<T>>, Result<T>>
{
    static_assert(is_supported_json_field_v<T>, "unsupported JSON field type");

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
    else if constexpr (std::is_same_v<T, bool>)
    {
        required_name = "boolean";
        optional_name = "a boolean";
    }
    else if constexpr (std::is_same_v<T, std::vector<std::string>>)
    {
        required_name = "array";
        optional_name = "an array of strings";
    }
    else
    {
        required_name = "object";
        optional_name = "an object of strings";
    }

    using ResultValue = std::conditional_t<mode == JsonFieldMode::optional_if_valid, std::optional<T>, T>;

    if (!input.contains(key))
    {
        if constexpr (mode == JsonFieldMode::required)
        {
            return fail<ResultValue>(error_kind, std::string{tool_name} + " requires " + std::string{required_name} + " field: " + key);
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
    else if constexpr (std::is_same_v<T, bool>)
    {
        matches_type = value.is_boolean();
    }
    else if constexpr (std::is_same_v<T, std::vector<std::string>>)
    {
        matches_type = value.is_array();
        if (matches_type)
        {
            for (const auto& item : value)
            {
                matches_type = matches_type && item.is_string();
            }
        }
    }
    else
    {
        matches_type = value.is_object();
        if (matches_type)
        {
            for (const auto& [_, item] : value.items())
            {
                matches_type = matches_type && item.is_string();
            }
        }
    }

    if (!matches_type)
    {
        if constexpr (mode == JsonFieldMode::optional_if_valid)
        {
            return std::optional<T>{};
        }
        else if constexpr (mode == JsonFieldMode::required)
        {
            return fail<ResultValue>(error_kind, std::string{tool_name} + " requires " + std::string{required_name} + " field: " + key);
        }
        else
        {
            return fail<ResultValue>(error_kind, std::string{tool_name} + ' ' + key + " must be " + std::string{optional_name});
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
auto read_optional_json_field(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name = {}, ErrorKind error_kind = ErrorKind::InvalidArgument) -> Result<std::optional<T>>
{
    static_assert(is_supported_json_field_v<T>, "unsupported JSON field type");

    if (!input.contains(std::string{field_name}))
    {
        return std::optional<T>{};
    }

    auto value = read_json_field<T>(input, field_name, tool_name, {}, error_kind);
    if (!value)
    {
        return nonstd::make_unexpected(value.error());
    }

    return std::optional<T>{std::move(*value)};
}

template <typename T>
auto read_nullable_json_field(const nlohmann::json& input,
                              std::string_view field_name,
                              std::string_view tool_name = {},
                              T default_value = {},
                              ErrorKind error_kind = ErrorKind::InvalidArgument) -> Result<T>
{
    static_assert(is_supported_json_field_v<T>, "unsupported JSON field type");

    const auto key = std::string{field_name};
    if (!input.contains(key) || input.at(key).is_null())
    {
        return default_value;
    }

    return read_json_field<T>(input, field_name, tool_name, {}, error_kind);
}

template <typename T>
auto read_nullable_optional_json_field(const nlohmann::json& input,
                                       std::string_view field_name,
                                       std::string_view tool_name = {},
                                       ErrorKind error_kind = ErrorKind::InvalidArgument) -> Result<std::optional<T>>
{
    static_assert(is_supported_json_field_v<T>, "unsupported JSON field type");

    const auto key = std::string{field_name};
    if (!input.contains(key) || input.at(key).is_null())
    {
        return std::optional<T>{};
    }

    return read_optional_json_field<T>(input, field_name, tool_name, error_kind);
}

template <typename T>
auto expect_json_field(Result<T> value) -> T
{
    if (!value)
    {
        throw nlohmann::json::type_error::create(302, value.error().message, nullptr);
    }

    return std::move(*value);
}

template <typename T>
auto optional_to_json(const std::optional<T>& value) -> nlohmann::json
{
    if (!value)
    {
        return nlohmann::json{};
    }

    return *value;
}

} // namespace codeharness
