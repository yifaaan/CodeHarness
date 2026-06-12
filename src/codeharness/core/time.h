#pragma once

#include <date/date.h>

#include <chrono>
#include <string>

namespace codeharness
{

inline auto UtcTimestampSeconds() -> std::string
{
    return date::format("%FT%TZ", date::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

} // namespace codeharness
