#pragma once

#include <absl/status/statusor.h>

#include "codeharness/config/setting.h"

namespace codeharness::config {
    [[nodiscard]] auto default_user_settings_path() -> std::filesystem::path;

    [[nodiscard]] auto load_settings_file(const std::filesystem::path& path)
        -> absl::StatusOr<std::optional<Settings>>;
}  // namespace codeharness::config