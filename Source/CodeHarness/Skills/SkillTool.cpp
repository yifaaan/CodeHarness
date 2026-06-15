#include "Skills/SkillTool.h"

#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"
#include "absl/status/status.h"

namespace codeharness::tools
{

	SkillTool::SkillTool(skills::SkillManager* manager)
		: manager(manager)
	{
	}

	std::string SkillTool::Description() const
	{
		return "Invoke a skill by name to inject domain-specific guidance into the conversation.";
	}

	nlohmann::json SkillTool::Parameters() const
	{
		return nlohmann::json{
			{"type", "object"},
			{"properties",
			 nlohmann::json{
				 {"name",
				  nlohmann::json{
					  {"type", "string"},
					  {"description", "Skill name to invoke"},
				  }},
				 {"args",
				  nlohmann::json{
					  {"type", "string"},
					  {"description", "Arguments to pass to the skill (optional)"},
				  }},
			 }},
			{"required", nlohmann::json::array({"name"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> SkillTool::ResolveExecution(const nlohmann::json& args)
	{
		if (!args.is_object())
			return absl::InvalidArgumentError("Arguments must be an object");

		if (!args.contains("name") || !args["name"].is_string())
			return absl::InvalidArgumentError("Missing or invalid 'name' argument");

		engine::ToolExecution exec;
		exec.description = "Invoke skill: " + args["name"].get<std::string>();
		exec.requiresPermission = false;

		return exec;
	}

	absl::StatusOr<engine::ToolResult> SkillTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (!manager)
			return absl::InternalError("SkillManager not set");

		std::string name = args["name"].get<std::string>();
		std::string skillArgs;

		if (args.contains("args") && args["args"].is_string())
			skillArgs = args["args"].get<std::string>();

		skills::SkillActivationPayload payload;
		payload.name = name;
		payload.args = skillArgs;
		payload.origin = skills::SkillOrigin::ModelTool;
		payload.depth = 0;

		auto status = manager->Activate(payload);

		engine::ToolResult result;
		if (!status.ok())
		{
			result.content = "Error activating skill: " + status.ToString();
			result.isError = true;
		}
		else
		{
			result.content = "Skill '" + name + "' activated successfully.";
			result.isError = false;
		}

		return result;
	}

} // namespace codeharness::tools