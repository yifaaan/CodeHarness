#pragma once

#include "codeharness/core/error.h"
#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace codeharness
{

// 解析工具 JSON 字段的统一 helper。
//
// 错误信息格式：`"<tool> requires <kind> field: <field_name>"` 或
// `"<tool> <field_name> must be a <kind>"`，由调用方提供前缀。

// 要求 input 含有 field_name 的 string 字段。
auto require_string(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name)
    -> Result<std::string>;

// 要求 input 含有 field_name 的 integer 字段。
auto require_int(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name)
    -> Result<int>;

// 要求 input 含有 field_name 的 boolean 字段。
auto require_bool(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name)
    -> Result<bool>;

// 读取可选 string 字段；缺失或类型不匹配返回 nullopt。
auto optional_string(const nlohmann::json& input, std::string_view field_name) -> std::optional<std::string>;

// 读取可选 int 字段；缺失返回 default_value；类型不匹配返回错误。
auto optional_int(const nlohmann::json& input, std::string_view field_name, int default_value,
                  std::string_view tool_name) -> Result<int>;

// 读取可选 bool 字段；缺失返回 default_value；类型不匹配返回错误。
auto optional_bool(const nlohmann::json& input, std::string_view field_name, bool default_value,
                   std::string_view tool_name) -> Result<bool>;

} // namespace codeharness
