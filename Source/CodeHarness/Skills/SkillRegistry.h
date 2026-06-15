#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "Host/Host.h"
#include "Skills/SkillTypes.h"

namespace codeharness::skills
{

	class SkillRegistry
	{
	public:
		SkillRegistry() = default;

		void LoadRoots(const std::vector<SkillRoot>& roots, host::Host* host);
		void Register(SkillDefinition skill);

		const SkillDefinition* GetSkill(std::string_view name) const;
		std::vector<const SkillDefinition*> ListSkills() const;
		std::vector<const SkillDefinition*> ListInvocableSkills() const;

		std::string RenderSkillPrompt(const SkillDefinition& skill, std::string_view rawArgs, const std::string& sessionId) const;

		// Render a catalog of model-invocable skills (those with
		// `disable_model_invocation == false`) for injection into the system
		// prompt so the model learns what skills exist and when to use them.
		// Returns an empty string when no skills are invocable, so callers can
		// unconditionally append the result without producing trailing noise.
		std::string RenderSkillIndex() const;

		void Clear();
		std::size_t Size() const;
		bool Empty() const;

	private:
		static std::string ExpandVariables(const std::string& content, const SkillDefinition& skill, std::string_view rawArgs, const std::string& sessionId);

		std::map<std::string, SkillDefinition> skills_;
	};

} // namespace codeharness::skills
