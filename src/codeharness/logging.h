#pragma once

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <utility>

namespace codeharness::logging {
    template <typename... Args>
    inline auto trace(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::trace("{}(): {}", scope,
                      fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline auto debug(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::debug("{}(): {}", scope,
                      fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline auto info(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::info("{}(): {}", scope,
                     fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline auto warn(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::warn("{}(): {}", scope,
                     fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline auto error(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::error("{}(): {}", scope,
                      fmt::format(fmt_str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    inline auto critical(const char* scope, fmt::format_string<Args...> fmt_str, Args&&... args)
        -> void {
        spdlog::critical("{}(): {}", scope,
                         fmt::format(fmt_str, std::forward<Args>(args)...));
    }
}  // namespace codeharness::logging

#define CH_LOG_TRACE(scope, ...) ::codeharness::logging::trace(scope, __VA_ARGS__)
#define CH_LOG_DEBUG(scope, ...) ::codeharness::logging::debug(scope, __VA_ARGS__)
#define CH_LOG_INFO(scope, ...) ::codeharness::logging::info(scope, __VA_ARGS__)
#define CH_LOG_WARN(scope, ...) ::codeharness::logging::warn(scope, __VA_ARGS__)
#define CH_LOG_ERROR(scope, ...) ::codeharness::logging::error(scope, __VA_ARGS__)
#define CH_LOG_CRITICAL(scope, ...) ::codeharness::logging::critical(scope, __VA_ARGS__)
