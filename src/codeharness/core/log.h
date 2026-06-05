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
        auto level = spdlog::level::from_str(env);
        if (level == spdlog::level::off)
        {
            level = spdlog::level::info;
        }
        spdlog::set_level(level);
    }
    else
    {
        spdlog::set_level(spdlog::level::info);
    }
}

} // namespace codeharness
