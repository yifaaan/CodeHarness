#pragma once

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>

namespace codeharness
{

// 初始化全局 logger。读取环境变量 LOG_CODEHARNESS_LEVEL（trace/debug/info/warn/error/critical）。
// 默认 info。未调用前 spdlog 默认级别是 off，所有日志都会被丢弃。
inline auto init_logger() -> void
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }
    initialized = true;

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

    if (const char* env = std::getenv("LOG_CODEHARNESS_LEVEL"))
    {
        const std::string level{env};
        if (level == "trace")
        {
            spdlog::set_level(spdlog::level::trace);
        }
        else if (level == "debug")
        {
            spdlog::set_level(spdlog::level::debug);
        }
        else if (level == "warn" || level == "warning")
        {
            spdlog::set_level(spdlog::level::warn);
        }
        else if (level == "error")
        {
            spdlog::set_level(spdlog::level::err);
        }
        else if (level == "critical")
        {
            spdlog::set_level(spdlog::level::critical);
        }
        else
        {
            spdlog::set_level(spdlog::level::info);
        }
    }
    else
    {
        spdlog::set_level(spdlog::level::info);
    }
}

} // namespace codeharness
