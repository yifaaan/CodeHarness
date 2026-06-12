#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness {

enum class JsonFieldMode {
  kRequired,
  kOptionalWithDefault,
  kOptionalIfValid,
};

template <typename T>
inline constexpr bool kIsSupportedJsonField =
    std::is_same_v<T, std::string> || std::is_same_v<T, int> || std::is_same_v<T, bool> ||
    std::is_same_v<T, std::vector<std::string>> || std::is_same_v<T, std::map<std::string, std::string>>;

// Parses a JSON field from a tool input object.
// Returns the value on success, or an absl::Status error on failure.
template <typename T, JsonFieldMode mode = JsonFieldMode::kRequired>
std::conditional_t<mode == JsonFieldMode::kOptionalIfValid, absl::StatusOr<std::optional<T>>, absl::StatusOr<T>>
ReadJsonField(const nlohmann::json& input, std::string_view field_name, std::string_view tool_name = {},
              T default_value = {}) {
  static_assert(kIsSupportedJsonField<T>, "unsupported JSON field type");

  auto required_name = std::string_view{};
  auto optional_name = std::string_view{};
  bool matches_type = false;
  const auto key = std::string{field_name};

  if constexpr (std::is_same_v<T, std::string>) {
    required_name = "string";
    optional_name = "a string";
  } else if constexpr (std::is_same_v<T, int>) {
    required_name = "integer";
    optional_name = "an integer";
  } else if constexpr (std::is_same_v<T, bool>) {
    required_name = "boolean";
    optional_name = "a boolean";
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    required_name = "array";
    optional_name = "an array of strings";
  } else {
    required_name = "object";
    optional_name = "an object of strings";
  }

  using ResultValue = std::conditional_t<mode == JsonFieldMode::kOptionalIfValid, std::optional<T>, T>;

  if (!input.contains(key)) {
    if constexpr (mode == JsonFieldMode::kRequired) {
      return absl::InvalidArgumentError(std::string{tool_name} + " requires " + std::string{required_name} +
                                        " field: " + key);
    } else if constexpr (mode == JsonFieldMode::kOptionalIfValid) {
      return std::optional<T>{};
    } else {
      return default_value;
    }
  }

  const auto& value = input.at(key);

  if constexpr (std::is_same_v<T, std::string>) {
    matches_type = value.is_string();
  } else if constexpr (std::is_same_v<T, int>) {
    matches_type = value.is_number_integer();
  } else if constexpr (std::is_same_v<T, bool>) {
    matches_type = value.is_boolean();
  } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
    matches_type = value.is_array();
    if (matches_type) {
      for (const auto& item : value) {
        matches_type = matches_type && item.is_string();
      }
    }
  } else {
    matches_type = value.is_object();
    if (matches_type) {
      for (const auto& [_, item] : value.items()) {
        matches_type = matches_type && item.is_string();
      }
    }
  }

  if (!matches_type) {
    if constexpr (mode == JsonFieldMode::kOptionalIfValid) {
      return std::optional<T>{};
    } else if constexpr (mode == JsonFieldMode::kRequired) {
      return absl::InvalidArgumentError(std::string{tool_name} + " requires " + std::string{required_name} +
                                        " field: " + key);
    } else {
      return absl::InvalidArgumentError(std::string{tool_name} + ' ' + key + " must be " + std::string{optional_name});
    }
  }

  if constexpr (mode == JsonFieldMode::kOptionalIfValid) {
    return std::optional<T>{value.template get<T>()};
  } else {
    return value.template get<T>();
  }
}

// Strict optional field: returns nullopt if missing, error if type mismatch.
template <typename T>
absl::StatusOr<std::optional<T>> ReadOptionalJsonField(const nlohmann::json& input, std::string_view field_name,
                                                       std::string_view tool_name = {}) {
  static_assert(kIsSupportedJsonField<T>, "unsupported JSON field type");

  if (!input.contains(std::string{field_name})) {
    return std::optional<T>{};
  }

  auto value = ReadJsonField<T>(input, field_name, tool_name);
  if (!value.ok()) {
    return value.status();
  }

  return std::optional<T>{std::move(*value)};
}

// Nullable field: returns default_value if missing or null.
template <typename T>
absl::StatusOr<T> ReadNullableJsonField(const nlohmann::json& input, std::string_view field_name,
                                        std::string_view tool_name = {}, T default_value = {}) {
  static_assert(kIsSupportedJsonField<T>, "unsupported JSON field type");

  const auto key = std::string{field_name};
  if (!input.contains(key) || input.at(key).is_null()) {
    return default_value;
  }

  return ReadJsonField<T>(input, field_name, tool_name);
}

// Nullable optional field: returns nullopt if missing or null.
template <typename T>
absl::StatusOr<std::optional<T>> ReadNullableOptionalJsonField(const nlohmann::json& input, std::string_view field_name,
                                                               std::string_view tool_name = {}) {
  static_assert(kIsSupportedJsonField<T>, "unsupported JSON field type");

  const auto key = std::string{field_name};
  if (!input.contains(key) || input.at(key).is_null()) {
    return std::optional<T>{};
  }

  return ReadOptionalJsonField<T>(input, field_name, tool_name);
}

// Throws on error (bridge to exceptions for JSON parsing contexts).
template <typename T>
T ExpectJsonField(absl::StatusOr<T> value) {
  if (!value.ok()) {
    throw nlohmann::json::type_error::create(302, std::string(value.status().message()), nullptr);
  }

  return std::move(*value);
}

template <typename T>
nlohmann::json OptionalToJson(const std::optional<T>& value) {
  if (!value) {
    return nlohmann::json{};
  }

  return *value;
}

}  // namespace codeharness
