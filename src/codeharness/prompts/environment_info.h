#pragma once

#include <filesystem>
#include <string>

namespace codeharness::prompts {

    // 当前运行环境信息
    struct EnvironmentInfo {
        std::string os_name;
        std::string architecture;
        std::string shell;
        std::filesystem::path cwd;
        std::filesystem::path home_dir;
        std::string date_utc;
    };

    // 收集当前运行环境信息
    [[nodiscard]] auto collect_environment(const std::filesystem::path& cwd) -> EnvironmentInfo;

    [[nodiscard]] auto format_environment_block(const EnvironmentInfo& env_info) -> std::string;

}  // namespace codeharness::prompts