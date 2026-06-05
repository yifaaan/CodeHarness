#pragma once

#include "codeharness/core/error.h"
#include "codeharness/core/result.h"

#include <cstdlib>
#include <filesystem>
#include <optional>

namespace codeharness
{

inline auto home_directory() -> std::optional<std::filesystem::path>
{
#ifdef _WIN32
    const auto* home = std::getenv("USERPROFILE");
#else
    const auto* home = std::getenv("HOME");
#endif

    if (home == nullptr || *home == '\0')
    {
        return std::nullopt;
    }

    return std::filesystem::path{home};
}

inline auto path_has_parent_reference(const std::filesystem::path& path) -> bool
{
    for (const auto& part : path)
    {
        if (part == "..")
        {
            return true;
        }
    }

    return false;
}

inline auto is_safe_relative_path(const std::filesystem::path& path) -> bool
{
    return !path.empty() && !path.is_absolute() && !path.has_root_name() && !path_has_parent_reference(path);
}

inline auto ensure_directory(const std::filesystem::path& path, std::string_view label = "directory") -> Result<void>
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error)
    {
        return fail<void>(ErrorKind::Io, "failed to create " + std::string{label} + ": " + error.message());
    }

    return {};
}

// Prefer codeharness/config/paths.h for new code.
[[deprecated("Use codeharness/config/paths.h instead")]]
inline auto default_codeharness_data_dir() -> std::optional<std::filesystem::path>
{
    const auto home = home_directory();
    if (!home)
    {
        return std::nullopt;
    }

    return *home / ".codeharness" / "data";
}

} // namespace codeharness
