#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "Skills/SkillTypes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::skills
{
	class SkillManager;
}

namespace codeharness::tools
{

	class SkillTool : public engine::ExecutableTool
	{
	public:
		explicit SkillTool(skills::SkillManager* manager);

		std::string Name() const override { return "skill"; }
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;

	private:
		skills::SkillManager* manager;
	};

} // namespace codeharness::tools
