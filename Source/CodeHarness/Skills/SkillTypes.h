#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::skills
{

	enum class SkillSource
	{
		Project,
		User,
		Extra,
		Builtin,
	};

	enum class SkillType
	{
		Prompt,
		Inline,
		Flow,
	};

	struct SkillMetadata
	{
		std::string name;
		std::string description;
		SkillType type = SkillType::Prompt;
		std::optional<std::string> whenToUse;
		bool disableModelInvocation = false;
		std::vector<std::string> arguments;
		std::optional<std::string> model;
	};

	struct SkillDefinition
	{
		std::string name;
		std::string description;
		std::string path;
		std::string dir;
		std::string content;
		SkillMetadata metadata;
		SkillSource source = SkillSource::Extra;
	};

	struct SkillRoot
	{
		std::string path;
		SkillSource source;
	};

	enum class SkillOrigin
	{
		UserSlash,
		ModelTool,
		NestedSkill,
	};

	struct SkillActivationPayload
	{
		std::string name;
		std::string args;
		SkillOrigin origin = SkillOrigin::UserSlash;
		int depth = 0;
	};

	std::string_view SkillTypeToString(SkillType type);
	std::optional<SkillType> ParseSkillType(std::string_view str);
	std::string_view SkillSourceToString(SkillSource source);
	std::optional<SkillSource> ParseSkillSource(std::string_view str);

} // namespace codeharness::skills
