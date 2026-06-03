#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <spdlog/spdlog.h>

namespace
{

struct SpdlogQuieter
{
    SpdlogQuieter()
    {
        spdlog::set_level(spdlog::level::off);
    }
};

} // namespace

static SpdlogQuieter g_spdlog_quieter{};