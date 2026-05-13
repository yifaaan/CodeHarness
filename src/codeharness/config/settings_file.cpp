#include "codeharness/config/settings_file.h"

#include <filesystem>
#include <fstream>
#include <optional>

#include "codeharness/config/setting.h"
#include "codeharness/logging.h"


namespace {
    using namespace codeharness;

    auto merge_api(config::ApiSettings& dst, const nlohmann::json& src) -> void {
        if (src.contains("base_url") and src.at("base_url").is_string()) {
            dst.base_url = src.at("base_url").get<std::string>();
        }
        if (src.contains("model") and src.at("model").is_string()) {
            dst.model = src.at("model").get<std::string>();
        }
        if (src.contains("max_tokens") and src.at("max_tokens").is_number_integer()) {
            dst.max_tokens = src.at("max_tokens").get<int>();
        }
        if (src.contains("timeout") and src.at("timeout").is_number_integer()) {
            dst.timeout = std::chrono::seconds(src.at("timeout").get<int>());
        }
    }

    void merge_permissions(permissions::PermissionSettings& dst, const nlohmann::json& j) {
        if (j.contains("mode") and j["mode"].is_string()) {
            dst.mode = permissions::parse_permission_mode(j["mode"].get<std::string>());
        }
        // TODO: 依次合并 allowed_tools / denied_tools / path_rules / denied_commands
    }
}  // namespace

namespace codeharness::config {
    auto default_user_settings_path() -> std::filesystem::path {
        // TODO: 需要从环境变量中获取用户配置文件路径
        return std::filesystem::current_path() / ".codeharness" / "settings.json";
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
        if (j.contains("api") and j["api"].is_object()) {
            merge_api(out.api, j["api"]);
        }
        if (j.contains("permissions") and j["permissions"].is_object()) {
            merge_permissions(out.permissions, j["permissions"]);
        }
        return out;
    }
}  // namespace codeharness::config