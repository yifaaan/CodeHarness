#include "codeharness/config/setting.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "codeharness/config/settings_file.h"
#include "codeharness/logging.h"

namespace {
    using namespace codeharness;

    [[nodiscard]] auto env_string(absl::string_view name) -> std::optional<std::string> {
#if defined(_WIN32)
        char* raw_value = nullptr;
        std::size_t value_size = 0;
        if (::_dupenv_s(&raw_value, &value_size, std::string{name}.c_str()) != 0 ||
            raw_value == nullptr) {
            return std::nullopt;
        }

        const auto owned_value = std::unique_ptr<char, decltype(&std::free)>{
            raw_value,
            &std::free,
        };
        const auto value = std::string{owned_value.get()};
#else
        const auto* raw_value = std::getenv(std::string{name}.c_str());
        if (raw_value == nullptr) {
            return std::nullopt;
        }
        const auto value = std::string{raw_value};
#endif

        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] auto env_int(const char* name) -> absl::StatusOr<std::optional<int>> {
        const auto value = env_string(name);
        if (!value.has_value()) {
            return std::optional<int>{};
        }

        auto parsed = int{};
        if (!absl::SimpleAtoi(*value, &parsed)) {
            return absl::InvalidArgumentError(
                absl::StrCat("environment variable ", name, " is not a valid integer"));
        }
        return parsed;
    }

    auto apply_overrides(config::Settings& settings, const config::SettingsOverrides& overrides)
        -> void {
        if (overrides.api_key.has_value()) {
            settings.api.api_key = *overrides.api_key;
            CH_LOG_DEBUG("config::load_settings", "api_key source=override");
        } else if (const auto api_key = env_string("OPENAI_API_KEY")) {
            settings.api.api_key = *api_key;
            CH_LOG_DEBUG("config::load_settings", "api_key source=OPENAI_API_KEY");
        } else {
            CH_LOG_DEBUG("config::load_settings", "api_key source=unset");
        }

        if (overrides.base_url.has_value()) {
            settings.api.base_url = *overrides.base_url;
            CH_LOG_DEBUG("config::load_settings", "base_url source=override value={}",
                         settings.api.base_url);
        } else if (const auto base_url = env_string("OPENAI_BASE_URL")) {
            settings.api.base_url = *base_url;
            CH_LOG_DEBUG("config::load_settings", "base_url source=OPENAI_BASE_URL value={}",
                         settings.api.base_url);
        }

        if (overrides.model.has_value()) {
            settings.api.model = *overrides.model;
            CH_LOG_DEBUG("config::load_settings", "model source=override value={}",
                         settings.api.model);
        } else if (const auto model = env_string("OPENAI_MODEL")) {
            settings.api.model = *model;
            CH_LOG_DEBUG("config::load_settings", "model source=OPENAI_MODEL value={}",
                         settings.api.model);
        }

        if (overrides.max_tokens.has_value()) {
            settings.api.max_tokens = *overrides.max_tokens;
            CH_LOG_DEBUG("config::load_settings", "max_tokens source=override value={}",
                         settings.api.max_tokens);
        } else {
            auto max_tokens = env_int("OPENAI_MAX_TOKENS");
            if (!max_tokens.ok()) {
                CH_LOG_ERROR("config::load_settings",
                             "max_tokens source=OPENAI_MAX_TOKENS error={}",
                             max_tokens.status().message());
            }
            if (max_tokens->has_value()) {
                settings.api.max_tokens = **max_tokens;
                CH_LOG_DEBUG("config::load_settings",
                             "max_tokens source=OPENAI_MAX_TOKENS value={}",
                             settings.api.max_tokens);
            }
        }
        if (overrides.permission_mode.has_value()) {
            settings.permissions.mode = *overrides.permission_mode;
            CH_LOG_DEBUG("config::load_settings", "permission_mode source=override");
        }
    }

    auto merge_settings(config::Settings& dst, const config::Settings& src) -> void {
        dst.api = src.api;
        dst.permissions = src.permissions;
    }
}  // namespace
namespace codeharness::config {
    auto load_settings(const SettingsOverrides& overrides) -> absl::StatusOr<Settings> {
        auto settings = Settings{};

        if (const auto path = default_user_settings_path(); !path.empty()) {
            auto file_or = config::load_settings_file(path);
            if (!file_or.ok()) {
                return file_or.status();
            }
            if (file_or->has_value()) {
                merge_settings(settings, **file_or);
            }
        }
        apply_overrides(settings, overrides);

        CH_LOG_DEBUG("config::load_settings",
                     "resolved base_url={} model={} max_tokens={} api_key_present={}",
                     settings.api.base_url, settings.api.model, settings.api.max_tokens,
                     !settings.api.api_key.empty());

        return settings;
    }
}  // namespace codeharness::config
