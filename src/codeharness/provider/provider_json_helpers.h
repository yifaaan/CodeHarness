#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace codeharness {

// Safely extract a string field from a JSON object, returning empty string
// when the field is missing, null, or not a string.
inline auto json_string_value(const nlohmann::json& input, std::string_view key) -> std::string {
  const auto found = input.find(key);
  if (found == input.end() || found->is_null() || !found->is_string()) {
    return {};
  }
  return found->get<std::string>();
}

// Attempt to parse a JSON payload, returning nullopt on parse failure rather
// than throwing.
inline auto try_parse_json(std::string_view payload) -> std::optional<nlohmann::json> {
  try {
    return nlohmann::json::parse(payload);
  } catch (const nlohmann::json::exception&) {
    return std::nullopt;
  }
}

}  // namespace codeharness
