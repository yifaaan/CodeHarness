#include "codeharness/config/settings_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "codeharness/config/paths.h"
#include "codeharness/config/setting.h"
#include "codeharness/logging.h"

namespace codeharness::config {
    auto default_user_settings_path() -> std::filesystem::path {
        return paths::user_settings_json_path(/*create_if_missing=*/true);
    }

    auto load_settings_file(const std::filesystem::path& path)
        -> absl::StatusOr<std::optional<Settings>> {
        if (not std::filesystem::exists(path)) {
            return std::nullopt;
        }
        std::ifstream in{path};
        if (not in) {
            CH_LOG_ERROR("config::load_settings_file", "failed to open settings file: {}",
                         path.string());
            return std::nullopt;
        }

        auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_discarded()) {
            CH_LOG_ERROR("config::load_settings_file", "invalid JSON: {}", path.string());
            return std::nullopt;
        }

        auto out = Settings{};

        if (j.contains("api") and j.at("api").is_object()) {
            const auto& a = j.at("api");
            if (a.contains("api_key") and a.at("api_key").is_string()) {
                out.api.api_key = a.at("api_key").get<std::string>();
            }
            if (a.contains("base_url") and a.at("base_url").is_string()) {
                out.api.base_url = a.at("base_url").get<std::string>();
            }
            if (a.contains("model") and a.at("model").is_string()) {
                out.api.model = a.at("model").get<std::string>();
            }
            if (a.contains("max_tokens") and a.at("max_tokens").is_number_integer()) {
                out.api.max_tokens = a.at("max_tokens").get<int>();
            }
            if (a.contains("timeout") and a.at("timeout").is_number_integer()) {
                out.api.timeout = std::chrono::seconds(a.at("timeout").get<int>());
            }
        }

        if (j.contains("permissions") and j.at("permissions").is_object()) {
            const auto& p = j.at("permissions");
            if (p.contains("mode") and p.at("mode").is_string()) {
                out.permissions.mode =
                    permissions::parse_permission_mode(p.at("mode").get<std::string>());
            }

            auto take_string_array = [&p](const char* key, std::vector<std::string>& dst) {
                if (not p.contains(key) or not p.at(key).is_array()) {
                    return;
                }
                dst.clear();
                for (const auto& item : p.at(key)) {
                    if (item.is_string()) {
                        dst.push_back(item.get<std::string>());
                    }
                }
            };
            take_string_array("allowed_tools", out.permissions.allowed_tools);
            take_string_array("denied_tools", out.permissions.denied_tools);
            take_string_array("denied_commands", out.permissions.denied_commands);

            if (p.contains("path_rules") and p.at("path_rules").is_array()) {
                auto rules = std::vector<permissions::PathRule>{};
                for (const auto& item : p.at("path_rules")) {
                    if (not item.is_object()) {
                        continue;
                    }
                    if (not item.contains("pattern") or not item.at("pattern").is_string()) {
                        continue;
                    }
                    rules.push_back(permissions::PathRule{
                        .pattern = item.at("pattern").get<std::string>(),
                        .allow = item.value("allow", true),
                    });
                }
                out.permissions.path_rules = std::move(rules);
            }
        }

        return out;
    }
}  // namespace codeharness::config
