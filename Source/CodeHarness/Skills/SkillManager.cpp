#include "Skills/SkillManager.h"

#include "Skills/SkillRegistry.h"
#include "absl/status/status.h"
#include "spdlog/spdlog.h"

namespace codeharness::skills
{

	SkillManager::SkillManager(SkillRegistry* registry)
		: registry(registry)
	{
	}

	void SkillManager::SetSessionId(std::string sessionId)
	{
		sessionId = std::move(sessionId);
	}

	void SkillManager::SetAppendMessageCallback(AppendMessageCallback callback)
	{
		appendMessage = std::move(callback);
	}

	void SkillManager::SetAppendSystemCallback(AppendSystemCallback callback)
	{
		appendSystem = std::move(callback);
	}

	absl::Status SkillManager::Activate(const SkillActivationPayload& payload)
	{
		if (!registry)
			return absl::InternalError("SkillRegistry not set");

		if (payload.depth > MAX_DEPTH)
			return absl::ResourceExhaustedError("Maximum skill recursion depth exceeded");

		auto* skill = registry->GetSkill(payload.name);
		if (!skill)
			return absl::NotFoundError("Skill not found: " + payload.name);

		auto renderedContent = registry->RenderSkillPrompt(*skill, payload.args, sessionId);

		// `prompt` skills inject as system content (turn-scoped guidance);
		// `inline` skills inject as a user message (the default model-invocation
		// path). `flow` is parsed but not yet implemented — fall back to inline
		// and warn so activation is still observable rather than a silent no-op.
		if (skill->metadata.type == SkillType::Flow)
		{
			spdlog::warn("skills: 'flow' type is not fully implemented; activating '{}' as inline", payload.name);
		}

		if (skill->metadata.type == SkillType::Prompt && appendSystem)
		{
			return appendSystem(std::string_view(renderedContent));
		}

		if (appendMessage)
		{
			return appendMessage(std::span<const char>(renderedContent.data(), renderedContent.size()));
		}

		return absl::OkStatus();
	}

} // namespace codeharness::skills
