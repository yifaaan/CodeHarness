#include <doctest/doctest.h>

#include "codeharness/prompts/environment_info.h"

using namespace codeharness;

TEST_CASE("environment block formats stable prompt section") {
    const auto info = prompts::EnvironmentInfo{
        .os_name = "Windows",
        .architecture = "x86_64",
        .shell = "powershell.exe",
        .cwd = std::filesystem::path{"D:/code/CodeHarness"},
        .home_dir = std::filesystem::path{"C:/Users/example"},
        .date_utc = "2026-05-15",
    };
    const auto block = prompts::format_environment_block(info);
    CHECK(block.find("<environment>") != std::string::npos);
    CHECK(block.find("OS: Windows") != std::string::npos);
    CHECK(block.find("Architecture: x86_64") != std::string::npos);
    CHECK(block.find("Shell: powershell.exe") != std::string::npos);
    CHECK(block.find("Working Directory:") != std::string::npos);
    CHECK(block.find("Home Directory:") != std::string::npos);
    CHECK(block.find("Date (UTC): 2026-05-15") != std::string::npos);
    CHECK(block.find("</environment>") != std::string::npos);
}

TEST_CASE("collect_environment returns basic runtime fields") {
    const auto cwd = std::filesystem::current_path();
    const auto info = prompts::collect_environment(cwd);

    CHECK(!info.os_name.empty());
    CHECK(!info.architecture.empty());
    CHECK(!info.shell.empty());
    CHECK(!info.cwd.empty());
    CHECK(!info.date_utc.empty());

    // YYYY-MM-DD
    CHECK(info.date_utc.size() == 10);
}