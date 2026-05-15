#include "codeharness/prompts/environment_info.h"

#include <date/date.h>

#include <filesystem>
#include <system_error>

#include "fmt/format.h"

namespace codeharness::prompts {

    auto collect_environment(const std::filesystem::path& cwd) -> EnvironmentInfo {
        auto info = EnvironmentInfo{};

#if defined(_WIN32)
        info.os_name = "Windows";
#elif defined(__APPLE__)
        info.os_name = "macOS";
#elif defined(__linux__)
        info.os_name = "Linux";
#else
        info.os_name = "Unknown";
#endif

#if defined(_M_X64) || defined(__x86_64__)
        info.architecture = "x86_64";
#elif defined(_M_IX86) || defined(__i386__)
        info.architecture = "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
        info.architecture = "ARM64";
#else
        info.architecture = "Unknown";
#endif

        std::error_code ec;
        auto resolved_cwd = std::filesystem::weakly_canonical(cwd, ec);
        info.cwd = ec ? std::filesystem::absolute(cwd) : std::move(resolved_cwd);

        // Windows COMSPEC；Unix-like  SHELL。
        if (const char* shell = std::getenv("SHELL"); shell != nullptr && shell[0] != '\0') {
            info.shell = std::filesystem::path{shell}.filename().string();
        } else if (const char* comspec = std::getenv("COMSPEC");
                   comspec != nullptr && comspec[0] != '\0') {
            info.shell = std::filesystem::path{comspec}.filename().string();
        } else {
            info.shell = "unknown";
        }

        if (const char* user_profile = std::getenv("USERPROFILE");
            user_profile != nullptr && user_profile[0] != '\0') {
            info.home_dir = user_profile;
        } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
            info.home_dir = home;
        }

        info.date_utc =
            date::format("%Y-%m-%d", date::floor<date::days>(std::chrono::system_clock::now()));
        return info;
    }

    auto format_environment_block(const EnvironmentInfo& env_info) -> std::string {
        auto out = std::string{};

        fmt::format_to(std::back_inserter(out), "<environment>\n");
        fmt::format_to(std::back_inserter(out), "OS: {}\n", env_info.os_name);
        fmt::format_to(std::back_inserter(out), "Architecture: {}\n", env_info.architecture);
        fmt::format_to(std::back_inserter(out), "Shell: {}\n", env_info.shell);
        fmt::format_to(std::back_inserter(out), "Working Directory: {}\n", env_info.cwd.string());

        if (!env_info.home_dir.empty()) {
            fmt::format_to(std::back_inserter(out), "Home Directory: {}\n",
                           env_info.home_dir.string());
        }

        fmt::format_to(std::back_inserter(out), "Date (UTC): {}\n", env_info.date_utc);
        fmt::format_to(std::back_inserter(out), "</environment>\n");
        return out;
    }
}  // namespace codeharness::prompts