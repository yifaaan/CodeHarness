#include "Skills/SkillRegistry.h"

#include "Skills/SkillScanner.h"

#include <algorithm>
#include <regex>

namespace codeharness::skills
{

	namespace
	{
		std::vector<std::string> SplitArgs(std::string_view rawArgs)
		{
			std::vector<std::string> args;
			std::string current;
			bool inQuote = false;

			for (char c : rawArgs)
			{
				if (c == '"')
				{
					inQuote = !inQuote;
				}
				else if (c == ' ' && !inQuote)
				{
					if (!current.empty())
					{
						args.push_back(current);
						current.clear();
					}
				}
				else
				{
					current += c;
				}
			}

			if (!current.empty())
				args.push_back(current);

			return args;
		}

		std::string ReplaceAll(std::string str, const std::string& from, const std::string& to)
		{
			if (from.empty())
				return str;

			std::string result;
			result.reserve(str.size());

			std::size_t pos = 0;
			std::size_t prev = 0;

			while ((pos = str.find(from, prev)) != std::string::npos)
			{
				result.append(str, prev, pos - prev);
				result.append(to);
				prev = pos + from.size();
			}

			result.append(str, prev, std::string::npos);
			return result;
		}
	} // namespace

	void SkillRegistry::LoadRoots(const std::vector<SkillRoot>& roots, host::Host* host)
	{
		auto skills = SkillScanner::Scan(roots, host);

		for (auto& skill : skills)
		{
			Register(std::move(skill));
		}
	}

	void SkillRegistry::Register(SkillDefinition skill)
	{
		if (skill.name.empty())
			return;

		if (skills_.find(skill.name) == skills_.end())
		{
			skills_.emplace(skill.name, std::move(skill));
		}
	}

	const SkillDefinition* SkillRegistry::GetSkill(std::string_view name) const
	{
		auto it = skills_.find(std::string(name));
		if (it != skills_.end())
			return &it->second;
		return nullptr;
	}

	std::vector<const SkillDefinition*> SkillRegistry::ListSkills() const
	{
		std::vector<const SkillDefinition*> result;
		result.reserve(skills_.size());

		for (const auto& [name, skill] : skills_)
		{
			result.push_back(&skill);
		}

		return result;
	}

	std::vector<const SkillDefinition*> SkillRegistry::ListInvocableSkills() const
	{
		std::vector<const SkillDefinition*> result;

		for (const auto& [name, skill] : skills_)
		{
			if (!skill.metadata.disableModelInvocation)
			{
				result.push_back(&skill);
			}
		}

		return result;
	}

	std::string SkillRegistry::RenderSkillPrompt(const SkillDefinition& skill, std::string_view rawArgs, const std::string& sessionId) const
	{
		return ExpandVariables(skill.content, skill, rawArgs, sessionId);
	}

	std::string SkillRegistry::RenderSkillIndex() const
	{
		// Only model-invocable skills are advertised (disable_model_invocation
		// filters out user-only skills). Each entry surfaces name, type, a
		// one-line description, and — when provided — the explicit when_to_use
		// hint so the model can decide whether to invoke the `skill` tool.
		auto invocable = ListInvocableSkills();
		if (invocable.empty())
			return {};

		std::string out;
		out += "\n\n## Available skills\n";
		out += "The following skills are available. Invoke one with the `skill` tool ";
		out += "(`{\"name\": \"...\", \"args\": \"...\"}`) when its description matches ";
		out += "the user's request.\n";

		for (const auto* skill : invocable)
		{
			out += "\n- **";
			out += skill->name;
			out += "** (";
			out += std::string(SkillTypeToString(skill->metadata.type));
			out += ")";
			if (!skill->description.empty())
			{
				out += ": ";
				out += skill->description;
			}
			if (skill->metadata.whenToUse && !skill->metadata.whenToUse->empty())
			{
				out += "\n  When to use: ";
				out += *skill->metadata.whenToUse;
			}
		}

		return out;
	}

	void SkillRegistry::Clear()
	{
		skills_.clear();
	}

	std::size_t SkillRegistry::Size() const
	{
		return skills_.size();
	}

	bool SkillRegistry::Empty() const
	{
		return skills_.empty();
	}

	std::string SkillRegistry::ExpandVariables(const std::string& content, const SkillDefinition& skill, std::string_view rawArgs, const std::string& sessionId)
	{
		std::string result = content;

		result = ReplaceAll(result, "$ARGUMENTS", std::string(rawArgs));

		auto args = SplitArgs(rawArgs);

		for (std::size_t i = 0; i < args.size() && i < 100; ++i)
		{
			result = ReplaceAll(result, "$" + std::to_string(i), args[i]);
		}

		if (!skill.metadata.arguments.empty())
		{
			for (std::size_t i = 0; i < skill.metadata.arguments.size(); ++i)
			{
				const auto& argName = skill.metadata.arguments[i];
				std::string value = (i < args.size()) ? args[i] : "";
				result = ReplaceAll(result, "$" + argName, value);
			}
		}

		result = ReplaceAll(result, "${KIMI_SKILL_DIR}", skill.dir);
		result = ReplaceAll(result, "${KIMI_SESSION_ID}", sessionId);

		return result;
	}

} // namespace codeharness::skills
