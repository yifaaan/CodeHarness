#include "codeharness/config/paths.h"

#include <absl/hash/hash.h>
#include <absl/strings/str_format.h>

#include <cstdlib>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace codeharness::config::paths {
    namespace {

        [[nodiscard]] auto env_string(const char* name) -> std::optional<std::string> {
#if defined(_WIN32)
            char* raw = nullptr;
            std::size_t size = 0;
            if (::_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
                return std::nullopt;
            }
            const auto owned =
                std::unique_ptr<char, decltype(&std::free)>{raw, &std::free};
            const auto value = std::string{owned.get()};
#else
            const auto* raw = std::getenv(name);
            if (raw == nullptr) {
                return std::nullopt;
            }
            const auto value = std::string{raw};
#endif
            if (value.empty()) {
                return std::nullopt;
            }
            return value;
        }

        [[nodiscard]] auto first_nonempty_env_path(const char* name) -> std::filesystem::path {
            const auto value = env_string(name);
            if (!value.has_value()) {
                return {};
            }
            return std::filesystem::path{*value};
        }

        // 跨平台「用户主目录」
        [[nodiscard]] auto home_directory() -> std::filesystem::path {
#if defined(_WIN32)
            if (const auto profile = env_string("USERPROFILE")) {
                return std::filesystem::path{*profile};
            }
            if (const auto home = env_string("HOME")) {
                return std::filesystem::path{*home};
            }
#else
            if (const auto home = env_string("HOME")) {
                return std::filesystem::path{*home};
            }
#endif

            return std::filesystem::current_path();
        }

        [[nodiscard]] auto normalize_dir(const std::filesystem::path& p) -> std::filesystem::path {
            if (p.empty()) {
                return p;
            }
            std::error_code ec;
            const auto canon = std::filesystem::weakly_canonical(p, ec);
            return ec ? std::filesystem::absolute(p) : canon;
        }

        auto ensure_dir(const std::filesystem::path& dir) -> void {
            if (dir.empty()) {
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            // 与 OpenHarness 的 mkdir(parents=True, exist_ok=True) 一致：失败时静默交给后续 IO
            // 报错也可
            (void)ec;
        }

    }  // namespace

    auto user_config_root(const bool create_if_missing) -> std::filesystem::path {
        // 1) 环境变量覆盖
        if (auto from_env = first_nonempty_env_path("CODEHARNESS_CONFIG_DIR"); !from_env.empty()) {
            auto out = normalize_dir(from_env);
            if (create_if_missing) {
                ensure_dir(out);
            }
            return out;
        }

        // 2) 默认 ~/.codeharness
        auto out = normalize_dir(home_directory() / ".codeharness");
        if (create_if_missing) {
            ensure_dir(out);
        }
        return out;
    }

    auto user_settings_json_path(const bool create_if_missing) -> std::filesystem::path {
        return user_config_root(create_if_missing) / "settings.json";
    }

    auto user_data_root(const bool create_if_missing) -> std::filesystem::path {
        if (auto from_env = first_nonempty_env_path("CODEHARNESS_DATA_DIR"); !from_env.empty()) {
            auto out = normalize_dir(from_env);
            if (create_if_missing) {
                ensure_dir(out);
            }
            return out;
        }

        auto out = normalize_dir(user_config_root(create_if_missing) / "data");
        if (create_if_missing) {
            ensure_dir(out);
        }
        return out;
    }

    auto user_logs_root(const bool create_if_missing) -> std::filesystem::path {
        if (auto from_env = first_nonempty_env_path("CODEHARNESS_LOGS_DIR"); !from_env.empty()) {
            auto out = normalize_dir(from_env);
            if (create_if_missing) {
                ensure_dir(out);
            }
            return out;
        }

        auto out = normalize_dir(user_config_root(create_if_missing) / "logs");
        if (create_if_missing) {
            ensure_dir(out);
        }
        return out;
    }

    auto user_sessions_root(const bool create_if_missing) -> std::filesystem::path {
        auto out = normalize_dir(user_data_root(create_if_missing) / "sessions");
        if (create_if_missing) {
            ensure_dir(out);
        }
        return out;
    }

    auto project_sessions_directory(const std::filesystem::path& cwd,
                                    const bool create_if_missing) -> std::filesystem::path {
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(cwd, ec);
        const auto base = ec ? std::filesystem::absolute(cwd) : resolved;

        auto stem = base.filename().string();
        if (stem.empty()) {
            stem = "workspace";
        }

        const auto key = base.string();
        const auto digest = static_cast<std::uint64_t>(absl::Hash<std::string>{}(key) & 0xFFFFFFFFFFFFULL);
        auto dir = user_sessions_root(create_if_missing) /
                   absl::StrFormat("%s-%012llx", stem, static_cast<unsigned long long>(digest));
        if (create_if_missing) {
            ensure_dir(dir);
        }
        return dir;
    }

}  // namespace codeharness::config::paths