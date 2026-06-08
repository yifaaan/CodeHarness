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

TEST_CASE("permission checker allows default mutating tool with matching path rule")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;
    settings.path_rules.push_back(codeharness::PermissionPathRule{
        .action = codeharness::PermissionAction::Allow,
        .pattern = "src/**",
        .tools = {"edit_file", "write_file"},
    });

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"src/main.cpp"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Allow);
}

TEST_CASE("permission checker path deny wins over read-only default allow")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;
    settings.path_rules.push_back(codeharness::PermissionPathRule{
        .action = codeharness::PermissionAction::Deny,
        .pattern = ".git/**",
    });

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{".git/config"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker plan mode blocks mutating tool despite path allow")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Plan;
    settings.path_rules.push_back(codeharness::PermissionPathRule{
        .action = codeharness::PermissionAction::Allow,
        .pattern = "src/**",
        .tools = {"write_file"},
    });

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"src/output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
    CHECK(decision.reason.find("plan mode") != std::string::npos);
}

TEST_CASE("permission checker command deny rule blocks bash in full_auto")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;
    settings.command_rules.push_back(codeharness::PermissionCommandRule{
        .action = codeharness::PermissionAction::Deny,
        .pattern = R"(\bgit\s+push\b)",
    });

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("bash", false, std::nullopt, std::string{"git push origin main"});

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker sensitive path deny wins over path allow")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;
    settings.path_rules.push_back(codeharness::PermissionPathRule{
        .action = codeharness::PermissionAction::Allow,
        .pattern = ".ssh/**",
    });

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{".ssh/id_rsa"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
    CHECK(decision.reason.find("sensitive path") != std::string::npos);
}

TEST_CASE("permission checker session grant allows same tool and path only")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;
    settings.session_grants.push_back(codeharness::PermissionSessionGrant{
        .tool_name = "write_file",
        .path = "output.txt",
    });

    codeharness::PermissionChecker checker{settings};

    auto same = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);
    auto other_path = checker.evaluate("write_file", false, std::filesystem::path{"other.txt"}, std::nullopt);
    auto other_tool = checker.evaluate("edit_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(same.action == codeharness::PermissionAction::Allow);
    CHECK(other_path.action == codeharness::PermissionAction::Ask);
    CHECK(other_tool.action == codeharness::PermissionAction::Ask);
}

TEST_CASE("permission checker session grant allows same command only")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;
    settings.session_grants.push_back(codeharness::PermissionSessionGrant{
        .tool_name = "bash",
        .command = "git status",
    });

    codeharness::PermissionChecker checker{settings};

    auto same = checker.evaluate("bash", false, std::nullopt, std::string{"git status"});
    auto other = checker.evaluate("bash", false, std::nullopt, std::string{"git diff"});

    CHECK(same.action == codeharness::PermissionAction::Allow);
    CHECK(other.action == codeharness::PermissionAction::Ask);
}

TEST_CASE("permission checker hard denies win over session grants")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;
    settings.session_grants.push_back(codeharness::PermissionSessionGrant{
        .tool_name = "read_file",
        .path = ".ssh/id_rsa",
    });
    settings.session_grants.push_back(codeharness::PermissionSessionGrant{
        .tool_name = "bash",
        .command = "rm -rf /",
    });

    codeharness::PermissionChecker checker{settings};

    auto path = checker.evaluate("read_file", true, std::filesystem::path{".ssh/id_rsa"}, std::nullopt);
    auto command = checker.evaluate("bash", false, std::nullopt, std::string{"rm -rf /"});

    CHECK(path.action == codeharness::PermissionAction::Deny);
    CHECK(command.action == codeharness::PermissionAction::Deny);
}
