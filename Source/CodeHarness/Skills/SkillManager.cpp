#include "Skills/SkillManager.h"

#include "Skills/SkillRegistry.h"
#include "absl/status/status.h"

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

		if (appendMessage)
		{
			return appendMessage(std::span<const char>(renderedContent.data(), renderedContent.size()));
		}

		return absl::OkStatus();
	}

} // namespace codeharness::skills