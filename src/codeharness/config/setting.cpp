#include "codeharness/config/setting.h"

#include <cstdlib>
#include <optional>

namespace {
    using namespace codeharness;

    [[nodiscard]] auto env_string(const char* name) -> std::optional<std::string> {
        auto value = std::getenv(name);
        if (not value || std::string{value}.empty()) {
            return std::nullopt;
        }
        return std::string{value};
    }

    [[nodiscard]] auto env_int(const char* name) -> std::optional<int> {
        const auto value = env_string(name);
        if (!value.has_value()) {
            return std::nullopt;
        }
        return std::stoi(*value);
    }
}  // namespace
namespace codeharness::config {
    auto load_settings(const SettingsOverrides& overrides) -> Settings {
        auto settings = Settings{};

        if (overrides.api_key.has_value()) {
            settings.api.api_key = *overrides.api_key;
        }
        if (overrides.base_url.has_value()) {
            settings.api.base_url = *overrides.base_url;
        }
        if (overrides.model.has_value()) {
            settings.api.model = *overrides.model;
        }
        if (overrides.max_tokens.has_value()) {
            settings.api.max_tokens = *overrides.max_tokens;
        }
        if (overrides.max_tokens.has_value()) {
            settings.api.max_tokens = *overrides.max_tokens;
        }
        if (overrides.permission_mode.has_value()) {
            settings.permissions.mode = *overrides.permission_mode;
        }

        return settings;
    }
}  // namespace codeharness::config