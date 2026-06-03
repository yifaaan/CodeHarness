#include "test_support.h"

TEST_CASE("permission checker allows read-only tools in default mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{"hello.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Allow);
}

TEST_CASE("permission checker asks for mutating tools in default mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Ask);
}

TEST_CASE("permission checker denies mutating tools in plan mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Plan;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker allows mutating tools in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Allow);
}

TEST_CASE("permission checker blocks sensitive paths even in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{".ssh/id_rsa"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker blocks dangerous commands even in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("bash", false, std::nullopt, std::string{"printf 'rm -rf /'"});

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker denied_tools wins over allowed_tools")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;
    settings.allowed_tools.push_back("write_file");
    settings.denied_tools.push_back("write_file");

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}
