#include <doctest/doctest.h>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <string>

#include "codeharness/tools/ask_user_question_tool.h"

using namespace codeharness;

TEST_CASE("ask user question tool returns the prompt answer") {
    auto tool = tools::AskUserQuestionTool{};
    auto seen_question = std::string{};

    const auto result = tool.execute(
        nlohmann::json{{"question", "Continue?"}},
        tools::ToolExecutionContext{
            .cwd = std::filesystem::current_path(),
            .ask_user_prompt = [&seen_question](absl::string_view question)
                -> absl::StatusOr<std::string> {
                seen_question = std::string{question};
                return "  yes  ";
            },
        });

    REQUIRE(result.ok());
    CHECK(*result == "yes");
    CHECK(seen_question == "Continue?");
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("ask user question tool reports no response for blank answers") {
    auto tool = tools::AskUserQuestionTool{};

    const auto result = tool.execute(
        nlohmann::json{{"question", "Continue?"}},
        tools::ToolExecutionContext{
            .cwd = std::filesystem::current_path(),
            .ask_user_prompt = [](absl::string_view) -> absl::StatusOr<std::string> {
                return "  \t  ";
            },
        });

    REQUIRE(result.ok());
    CHECK(*result == "(no response)");
}

TEST_CASE("ask user question tool validates input and availability") {
    auto tool = tools::AskUserQuestionTool{};
    const auto context = tools::ToolExecutionContext{.cwd = std::filesystem::current_path()};

    const auto empty_question = tool.execute(nlohmann::json{{"question", ""}}, context);
    REQUIRE_FALSE(empty_question.ok());
    CHECK(empty_question.status().message() == "question must not be empty");

    const auto unavailable = tool.execute(nlohmann::json{{"question", "Continue?"}}, context);
    REQUIRE_FALSE(unavailable.ok());
    CHECK(unavailable.status().code() == absl::StatusCode::kUnavailable);
    CHECK(unavailable.status().message() == "ask_user_question is unavailable in this session");
}
