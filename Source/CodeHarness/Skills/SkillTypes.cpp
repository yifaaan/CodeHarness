#include "Skills/SkillTypes.h"

namespace codeharness::skills
{

	std::string_view SkillTypeToString(SkillType type)
	{
		switch (type)
		{
		case SkillType::Prompt:
			return "prompt";
		case SkillType::Inline:
			return "inline";
		case SkillType::Flow:
			return "flow";
		}
		return "prompt";
	}

	std::optional<SkillType> ParseSkillType(std::string_view str)
	{
		if (str == "prompt")
			return SkillType::Prompt;
		if (str == "inline")
			return SkillType::Inline;
		if (str == "flow")
			return SkillType::Flow;
		return std::nullopt;
	}

	std::string_view SkillSourceToString(SkillSource source)
	{
		switch (source)
		{
		case SkillSource::Project:
			return "project";
		case SkillSource::User:
			return "user";
		case SkillSource::Extra:
			return "extra";
		case SkillSource::Builtin:
			return "builtin";
		}
		return "extra";
	}

	std::optional<SkillSource> ParseSkillSource(std::string_view str)
	{
		if (str == "project")
			return SkillSource::Project;
		if (str == "user")
			return SkillSource::User;
		if (str == "extra")
			return SkillSource::Extra;
		if (str == "builtin")
			return SkillSource::Builtin;
		return std::nullopt;
	}

} // namespace codeharness::skills