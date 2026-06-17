#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "Skills/SkillTypes.h"
#include "absl/status/status.h"

namespace codeharness::skills
{
	class SkillRegistry;
}

namespace codeharness::skills
{

	using AppendMessageCallback = std::function<absl::Status(std::span<const char>)>;
	using AppendSystemCallback = std::function<absl::Status(std::string_view)>;

	class SkillManager
	{
	public:
		SkillManager(SkillRegistry* registry);

		void SetSessionId(std::string sessionId);

		// Set the callback invoked after an *inline* skill is rendered — the
		// rendered content becomes a user-role message in the conversation.
		// Called with the rendered (variable-expanded) skill content.
		void SetAppendMessageCallback(AppendMessageCallback callback);

		// Set the callback invoked after a *prompt* skill is rendered — the
		// rendered content is appended to the system prompt for the turn.
		// Called with the rendered (variable-expanded) skill content.
		void SetAppendSystemCallback(AppendSystemCallback callback);

		absl::Status Activate(const SkillActivationPayload& payload);

		SkillRegistry* GetRegistry() const
		{
			return registry;
		}

		static constexpr int MAX_DEPTH = 3;

	private:
		SkillRegistry* registry;
		AppendMessageCallback appendMessage;
		AppendSystemCallback appendSystem;
		std::string sessionId;
	};

} // namespace codeharness::skills
