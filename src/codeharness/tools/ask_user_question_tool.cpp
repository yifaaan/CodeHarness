#include "codeharness/tools/ask_user_question_tool.h"

#include <absl/status/status.h>
#include <absl/strings/ascii.h>
#include <absl/strings/string_view.h>

#include <string>

namespace codeharness::tools {

    auto AskUserQuestionTool::name() const -> absl::string_view { return "ask_user_question"; }

    auto AskUserQuestionTool::description() const -> absl::string_view {
        return "Ask the interactive user a follow-up question and return the answer.";
    }

    auto AskUserQuestionTool::input_schema() const -> nlohmann::json {
        return {
            {"type", "object"},
            {"properties",
             {
                 {"question",
                  {{"type", "string"}, {"description", "The exact question to ask the user."}}},
             }},
            {"required", {"question"}},
            {"additionalProperties", false},
        };
    }

    auto AskUserQuestionTool::is_read_only(const nlohmann::json& input) const -> bool {
        static_cast<void>(input);
        return true;
    }

    auto AskUserQuestionTool::execute(const nlohmann::json& input,
                                      const ToolExecutionContext& ctx)
        -> absl::StatusOr<std::string> {
        const auto question = input.at("question").get<std::string>();
        if (question.empty()) {
            return absl::InvalidArgumentError("question must not be empty");
        }
        if (!ctx.ask_user_prompt) {
            return absl::UnavailableError("ask_user_question is unavailable in this session");
        }

        auto answer = ctx.ask_user_prompt(question);
        if (!answer.ok()) {
            return answer.status();
        }

        auto output = *std::move(answer);
        absl::StripAsciiWhitespace(&output);
        if (output.empty()) {
            return "(no response)";
        }
        return output;
    }

}  // namespace codeharness::tools
