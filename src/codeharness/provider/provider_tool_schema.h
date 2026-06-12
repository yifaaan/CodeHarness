#pragma once

#include <nlohmann/json.hpp>
#include <string_view>

namespace codeharness {

auto loose_tool_input_schema() -> nlohmann::json;
auto parse_tool_input_json_or_empty_object(std::string_view value) -> nlohmann::json;

}  // namespace codeharness
