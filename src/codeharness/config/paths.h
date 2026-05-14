#pragma once

#include <filesystem>

namespace codeharness::config::paths {

    // 用户级配置根目录：优先环境变量 CODEHARNESS_CONFIG_DIR，否则为 ~/.codeharness（Windows
    // 为 %USERPROFILE%\.codeharness）。 create_if_missing == true 时，会尝试创建目录
    [[nodiscard]] auto user_config_root(bool create_if_missing = true) -> std::filesystem::path;

    // ~/.codeharness/settings.json（或环境变量指定目录下的 settings.json）
    [[nodiscard]] auto user_settings_json_path(bool create_if_missing = true)
        -> std::filesystem::path;

    // 数据目录：CODEHARNESS_DATA_DIR，否则为 user_config_root()/data
    [[nodiscard]] auto user_data_root(bool create_if_missing = true) -> std::filesystem::path;

    // 日志目录：CODEHARNESS_LOGS_DIR，否则为 user_config_root()/logs
    [[nodiscard]] auto user_logs_root(bool create_if_missing = true) -> std::filesystem::path;

    // 会话目录：user_data_root()/sessions
    [[nodiscard]] auto user_sessions_root(bool create_if_missing = true) -> std::filesystem::path;

    // 某工作目录对应的会话子目录：sessions/<目录名>-<指纹>
    [[nodiscard]] auto project_sessions_directory(const std::filesystem::path& cwd,
                                                  bool create_if_missing = true)
        -> std::filesystem::path;

}  // namespace codeharness::config::paths