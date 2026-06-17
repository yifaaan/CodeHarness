#include "Desktop/DesktopEventJson.h"

#include <string>
#include <utility>

#include "Rpc/RpcTypes.h"

namespace codeharness::desktop
{

	DesktopEvent CoreEventToDesktopEvent(const rpc::CoreEvent& event)
	{
		auto wire = rpc::CoreEventToJson(event);
		auto type = wire.value("event", "unknown");
		return DesktopEvent{
			.sessionId = event.sessionId,
			.agentId = event.agentId,
			.type = std::move(type),
			.payload = std::move(wire),
		};
	}

	DesktopSessionItem SessionInfoToDesktopItem(const session::SessionInfo& info)
	{
		return DesktopSessionItem{
			.sessionId = info.sessionId,
			.title = info.title,
			.workdir = info.workdir,
			.createdAt = info.createdAt,
			.updatedAt = info.updatedAt,
		};
	}

	nlohmann::json DesktopEventToJson(const DesktopEvent& event)
	{
		return {
			{"session_id", event.sessionId},
			{"agent_id", event.agentId},
			{"type", event.type},
			{"payload", event.payload},
		};
	}

	nlohmann::json DesktopSessionItemToJson(const DesktopSessionItem& item)
	{
		return {
			{"session_id", item.sessionId},
			{"title", item.title},
			{"workdir", item.workdir},
			{"created_at", item.createdAt},
			{"updated_at", item.updatedAt},
		};
	}

} // namespace codeharness::desktop
