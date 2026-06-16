#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Tool.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	struct QuestionRequest
	{
		std::string question;
		std::vector<std::string> options;
		bool allowFreeform = true;
	};

	using QuestionCallback = std::function<std::string(const QuestionRequest&)>;

	class AskUserTool : public engine::ExecutableTool
	{
	public:
		explicit AskUserTool(QuestionCallback callback = {});

		std::string Name() const override { return "AskUser"; }
		std::string Description() const override;
		nlohmann::json Parameters() const override;
		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;

	private:
		QuestionCallback callback;
	};

} // namespace codeharness::tools
