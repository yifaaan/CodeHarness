#pragma once

#include "absl/strings/string_view.h"

namespace codeharness::permissions {
    enum class PermissionMode {
        default_mode,
        full_auto,
        plan,
    };

    [[nodiscard]] constexpr auto parse_permission_mode(absl::string_view mode) -> PermissionMode {
        if (mode == "default") {
            return PermissionMode::default_mode;
        }
        if (mode == "full_auto") {
            return PermissionMode::full_auto;
        }
        if (mode == "plan") {
            return PermissionMode::plan;
        }
        return PermissionMode::default_mode;
    }
}  // namespace codeharness::permissions
