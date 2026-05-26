#include "codeharness/tools/config_tool.h"

#include <absl/status/status.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "codeharness/config/setting.h"
#include "codeharness/config/settings_file.h"
#include "codeharness/permissions/models.h"

namespace codeharness::tools {
    namespace {

        [[nodiscard]] auto permission_mode_to_string(permissions::PermissionMode mode)
            -> std::string {
            switch (mode) {
                case permissions::PermissionMode::full_auto:
                    return "full_auto";
                case permissions::PermissionMode::plan:
                    return "plan";
                case permissions::PermissionMode::default_mode:
                    return "default";
            }
            return "default";
        }

        [[nodiscard]] auto settings_to_json(const config::Settings& settings) -> nlohmann::json {
            auto path_rules = nlohmann::json::array();
            for (const auto& rule : settings.permissions.path_rules) {
                path_rules.push_back({
                    {"pattern", rule.pattern},
                    {"allow", rule.allow},
                });
            }

            return {
                {"api",
                 {
                     {"api_key", settings.api.api_key},
                     {"base_url", settings.api.base_url},
                     {"model", settings.api.model},
                     {"max_tokens", settings.api.max_tokens},
                     {"timeout", static_cast<int>(settings.api.timeout.count())},
                 }},
                {"permissions",
                 {
                     {"mode", permission_mode_to_string(settings.permissions.mode)},
                     {"allowed_tools", settings.permissions.allowed_tools},
                     {"denied_tools", settings.permissions.denied_tools},
                     {"denied_commands", settings.permissions.denied_commands},
                     {"path_rules", path_rules},
                 }},
            };
        }

        [[nodiscard]] auto load_raw_settings_json(const std::filesystem::path& path)
            -> absl::StatusOr<nlohmann::json> {
            if (!std::filesystem::exists(path)) {
                return nlohmann::json::object();
            }

            std::ifstream file{path, std::ios::in};
            if (!file.is_open()) {
                return absl::PermissionDeniedError(
                    absl::StrCat("failed to open settings file: ", path.string()));
            }

            auto json = nlohmann::json::parse(file, nullptr, false);
            if (json.is_discarded() || !json.is_object()) {
                return absl::InvalidArgumentError(
                    absl::StrCat("invalid settings JSON: ", path.string()));
            }

            return json;
        }

        [[nodiscard]] auto save_raw_settings_json(const std::filesystem::path& path,
                                                  const nlohmann::json& json) -> absl::Status {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return absl::InternalError(
                    absl::StrCat("failed to create settings directory: ", ec.message()));
            }

            std::ofstream file{path, std::ios::out | std::ios::trunc};
            if (!file.is_open()) {
                return absl::PermissionDeniedError(
                    absl::StrCat("failed to open settings file for writing: ", path.string()));
            }

            file << json.dump(2) << '\n';
            file.close();
            if (file.fail()) {
                return absl::InternalError(
                    absl::StrCat("failed to write settings file: ", path.string()));
            }

            return absl::OkStatus();
        }

        [[nodiscard]] auto set_config_value(nlohmann::json& json,
                                            const std::string& key,
                                            const std::string& value) -> absl::Status {
            auto& api = json["api"];
            if (!api.is_object()) {
                api = nlohmann::json::object();
            }
            auto& permissions = json["permissions"];
            if (!permissions.is_object()) {
                permissions = nlohmann::json::object();
            }

            if (key == "api_key" || key == "api.api_key") {
                api["api_key"] = value;
                return absl::OkStatus();
            }
            if (key == "base_url" || key == "api.base_url") {
                api["base_url"] = value;
                return absl::OkStatus();
            }
            if (key == "model" || key == "api.model") {
                api["model"] = value;
                return absl::OkStatus();
            }
            if (key == "max_tokens" || key == "api.max_tokens") {
                auto parsed = int{};
                if (!absl::SimpleAtoi(value, &parsed) || parsed <= 0) {
                    return absl::InvalidArgumentError("max_tokens must be a positive integer");
                }
                api["max_tokens"] = parsed;
                return absl::OkStatus();
            }
            if (key == "timeout" || key == "api.timeout") {
                auto parsed = int{};
                if (!absl::SimpleAtoi(value, &parsed) || parsed <= 0) {
                    return absl::InvalidArgumentError("timeout must be a positive integer");
                }
                api["timeout"] = parsed;
                return absl::OkStatus();
            }
            if (key == "permission_mode" || key == "permissions.mode") {
                if (value != "default" && value != "plan" && value != "full_auto") {
                    return absl::InvalidArgumentError(
                        "permission_mode must be default, plan, or full_auto");
                }
                permissions["mode"] = value;
                return absl::OkStatus();
            }

            return absl::InvalidArgumentError(absl::StrCat("Unknown config key: ", key));
        }

    }  // namespace

    auto ConfigTool::name() const -> absl::string_view { return "config"; }

    auto ConfigTool::description() const -> absl::string_view {
        return "Read or update CodeHarness settings.";
    }

    auto ConfigTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"action",
                  {{"type", "string"}, {"description", "show or set"}, {"default", "show"}}},
                 {"key", {{"type", "string"}}},
                 {"value", {{"type", "string"}}},
             }},
            {"additionalProperties", false},
        };
    }

    auto ConfigTool::is_read_only(const nlohmann::json& input) const -> bool {
        return input.value("action", std::string{"show"}) != "set";
    }

    auto ConfigTool::execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        static_cast<void>(ctx);

        const auto action = input.value("action", std::string{"show"});
        if (action == "show") {
            const auto settings = config::load_settings();
            if (!settings.ok()) {
                return settings.status();
            }
            return settings_to_json(*settings).dump(2);
        }

        if (action != "set") {
            return absl::InvalidArgumentError("Usage: action=show or action=set with key/value");
        }
        if (!input.contains("key") || !input.at("key").is_string() || !input.contains("value") ||
            !input.at("value").is_string()) {
            return absl::InvalidArgumentError("Usage: action=show or action=set with key/value");
        }

        const auto path = config::default_user_settings_path();
        auto raw = load_raw_settings_json(path);
        if (!raw.ok()) {
            return raw.status();
        }

        const auto key = input.at("key").get<std::string>();
        const auto value = input.at("value").get<std::string>();
        if (const auto status = set_config_value(*raw, key, value); !status.ok()) {
            return status;
        }
        if (const auto status = save_raw_settings_json(path, *raw); !status.ok()) {
            return status;
        }

        return absl::StrCat("Updated ", key);
    }

}  // namespace codeharness::tools
