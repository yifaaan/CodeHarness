#pragma once

#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/core/strings.h"

// Skill frontmatter 解析的内部 helper。
//
// 错误策略：键缺失、类型不匹配、转换抛异常或 trim 后为空时一律"软失败" ——
// 返回 nullopt / fallback / 空 vector，由调用方决定如何兜底。
// skill loader 故意忽略掉单个字段的损坏，而不是整文件加载失败。

namespace codeharness::skills
{

// 读取可空标量字符串。键缺失、非标量、转换失败、trim 后为空时返回 nullopt。
inline auto yaml_get_string(const YAML::Node& node, std::string_view key) -> std::optional<std::string>
{
    const auto value = node[std::string{key}];
    if (!value || !value.IsScalar())
    {
        return std::nullopt;
    }

    try
    {
        auto text = std::string{trim(value.as<std::string>())};
        if (text.empty())
        {
            return std::nullopt;
        }
        return text;
    }
    catch (const YAML::Exception&)
    {
        return std::nullopt;
    }
}

// 读取布尔值，键缺失或转换失败时回退到 fallback。
inline auto yaml_get_bool(const YAML::Node& node, std::string_view key, bool fallback) -> bool
{
    const auto value = node[std::string{key}];
    if (!value)
    {
        return fallback;
    }

    try
    {
        return value.as<bool>();
    }
    catch (const YAML::Exception&)
    {
        return fallback;
    }
}

// 读取 string 列表：
//   - 键缺失：返回空 vector
//   - scalar：单元素 vector
//   - sequence：逐元素读取 trim 后非空的标量
//   - 其他类型：返回空 vector
inline auto yaml_get_string_list(const YAML::Node& node, std::string_view key) -> std::vector<std::string>
{
    const auto value = node[std::string{key}];
    if (!value)
    {
        return {};
    }

    if (value.IsScalar())
    {
        try
        {
            auto text = std::string{trim(value.as<std::string>())};
            if (!text.empty())
            {
                return {std::move(text)};
            }
        }
        catch (const YAML::Exception&)
        {
        }
        return {};
    }

    if (!value.IsSequence())
    {
        return {};
    }

    std::vector<std::string> result;
    for (const auto& item : value)
    {
        if (!item.IsScalar())
        {
            continue;
        }

        try
        {
            auto text = std::string{trim(item.as<std::string>())};
            if (!text.empty())
            {
                result.push_back(std::move(text));
            }
        }
        catch (const YAML::Exception&)
        {
        }
    }

    return result;
}

} // namespace codeharness::skills
