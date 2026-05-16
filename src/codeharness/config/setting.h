#pragma once

#include <absl/status/statusor.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "codeharness/permissions/checker.h"


namespace codeharness::config {
    struct ApiSettings {
        std::string api_key;
        std::string base_url{"https://api.openai.com/v1"};
        std::string model{"gpt-5.5"};
        int max_tokens{4096};
        std::chrono::seconds timeout{60};
    };

    struct Settings {
        ApiSettings api;
        permissions::PermissionSettings permissions;
    };

    struct SettingsOverrides {
        std::optional<std::string> api_key;
        std::optional<std::string> base_url;
        std::optional<std::string> model;
        std::optional<int> max_tokens;
        std::optional<permissions::PermissionMode> permission_mode;
        std::optional<std::vector<std::string>> allowed_tools;
        std::optional<std::vector<std::string>> denied_tools;
        std::optional<std::filesystem::path> settings_file;
    };

    [[nodiscard]] auto load_settings(const SettingsOverrides& overrides = {})
        -> absl::StatusOr<Settings>;

}  // namespace codeharness::config
