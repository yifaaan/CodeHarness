#pragma once

#include "Desktop/DesktopModels.h"
#include "Rpc/RpcTypes.h"
#include "Session/SessionTypes.h"

namespace codeharness::desktop
{

	extern DesktopEvent CoreEventToDesktopEvent(const rpc::CoreEvent& event);
	extern DesktopSessionItem SessionInfoToDesktopItem(const session::SessionInfo& info);
	extern nlohmann::json DesktopEventToJson(const DesktopEvent& event);
	extern nlohmann::json DesktopSessionItemToJson(const DesktopSessionItem& item);

} // namespace codeharness::desktop
