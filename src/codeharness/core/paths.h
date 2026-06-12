#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>

#include "absl/status/status.h"

namespace codeharness {

inline std::optional<std::filesystem::path> HomeDirectory() {
#ifdef _WIN32
  const auto* home = std::getenv("USERPROFILE");
#else
  const auto* home = std::getenv("HOME");
#endif

  if (home == nullptr || *home == '\0') {
    return std::nullopt;
  }

  return std::filesystem::path{home};
}

inline bool PathHasParentReference(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (part == "..") {
      return true;
    }
  }

  return false;
}

inline bool IsSafeRelativePath(const std::filesystem::path& path) {
  return !path.empty() && !path.is_absolute() && !path.has_root_name() && !PathHasParentReference(path);
}

inline absl::Status EnsureDirectory(const std::filesystem::path& path, std::string_view label = "directory") {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    return absl::InternalError("failed to create " + std::string{label} + ": " + error.message());
  }

  return absl::OkStatus();
}

}  // namespace codeharness
