#pragma once

#include <functional>
#include <span>
#include <string>

#include "Skills/SkillTypes.h"
#include "absl/status/status.h"

namespace codeharness::skills
{
	class SkillRegistry;
}

namespace codeharness::skills
{

	using AppendMessageCallback = std::function<absl::Status(std::span<const char>)>;

	class SkillManager
	{
	public:
		SkillManager(SkillRegistry* registry);

		void SetSessionId(std::string sessionId);

		// Set the callback invoked after a skill is rendered.
		// Called with the rendered (variable-expanded) skill content.
		void SetAppendMessageCallback(AppendMessageCallback callback);

		absl::Status Activate(const SkillActivationPayload& payload);

		SkillRegistry* GetRegistry() const { return registry; }

		static constexpr int MAX_DEPTH = 3;

	private:
		SkillRegistry* registry;
		AppendMessageCallback appendMessage;
		std::string sessionId;
	};

} // namespace codeharness::skills
