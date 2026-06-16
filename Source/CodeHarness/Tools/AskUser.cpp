#include "Tools/AskUser.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "fmt/format.h"

namespace codeharness::tools
{

	AskUserTool::AskUserTool(QuestionCallback callback) : callback(std::move(callback)) {}

	std::string AskUserTool::Description() const
	{
		return "Ask the user a blocking clarifying question. Use this only when progress depends on user input.";
	}

	nlohmann::json AskUserTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"question", {{"type", "string"}, {"description", "The question to present to the user."}}},
			  {"options",
			   {{"type", "array"},
				{"items", {{"type", "string"}}},
				{"description", "Optional suggested answers."}}},
			  {"allow_freeform",
			   {{"type", "boolean"}, {"default", true}, {"description", "Whether the user may type a custom answer."}}}}},
			{"required", nlohmann::json::array({"question"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> AskUserTool::ResolveExecution(const nlohmann::json& args)
	{
		auto question = args.value("question", std::string{});
		if (question.empty())
		{
			return absl::InvalidArgumentError("'question' is required");
		}
		return engine::ToolExecution{
			.description = fmt::format("Ask user: {}", question),
			.requiresPermission = false,
		};
	}

	absl::StatusOr<engine::ToolResult> AskUserTool::Execute(const nlohmann::json& args, const engine::ToolContext&)
	{
		if (!callback)
		{
			return absl::FailedPreconditionError("no question callback installed");
		}

		QuestionRequest request;
		request.question = args.value("question", std::string{});
		request.allowFreeform = args.value("allow_freeform", true);
		if (args.contains("options") && args["options"].is_array())
		{
			for (const auto& option : args["options"])
			{
				if (option.is_string())
				{
					request.options.push_back(option.get<std::string>());
				}
			}
		}
		if (request.question.empty())
		{
			return absl::InvalidArgumentError("'question' is required");
		}

		auto answer = callback(request);
		return engine::ToolResult{.content = answer.empty() ? std::string("(no answer)") : std::move(answer)};
	}

} // namespace codeharness::tools
