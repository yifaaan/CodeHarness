#include "Desktop/DesktopEventJson.h"

#include <doctest/doctest.h>

#include "Agent/AgentTypes.h"
#include "Engine/LoopTypes.h"
#include "Rpc/RpcTypes.h"
#include "Session/SessionTypes.h"

namespace desktop = codeharness::desktop;
namespace agent = codeharness::agent;
namespace engine = codeharness::engine;
namespace rpc = codeharness::rpc;
namespace session = codeharness::session;

TEST_CASE("DesktopEventJson: converts assistant delta core event")
{
	rpc::CoreEvent coreEvent;
	coreEvent.sessionId = "session-1";
	coreEvent.agentId = "main";
	coreEvent.event = agent::LoopEvent{engine::AssistantDeltaEvent{.text = "hello"}};

	auto event = desktop::CoreEventToDesktopEvent(coreEvent);
	CHECK(event.sessionId == "session-1");
	CHECK(event.agentId == "main");
	CHECK(event.type == "loop");
	CHECK(event.payload["loop_event"]["AssistantDelta"]["text"] == "hello");

	auto json = desktop::DesktopEventToJson(event);
	CHECK(json["session_id"] == "session-1");
	CHECK(json["type"] == "loop");
}

TEST_CASE("DesktopEventJson: converts session info")
{
	session::SessionInfo info;
	info.sessionId = "abc123";
	info.title = "Desktop";
	info.workdir = "D:/code/CodeHarness";
	info.createdAt = 10;
	info.updatedAt = 20;

	auto item = desktop::SessionInfoToDesktopItem(info);
	CHECK(item.sessionId == "abc123");
	CHECK(item.title == "Desktop");
	CHECK(item.workdir == "D:/code/CodeHarness");
	CHECK(item.createdAt == 10);
	CHECK(item.updatedAt == 20);

	auto json = desktop::DesktopSessionItemToJson(item);
	CHECK(json["session_id"] == "abc123");
	CHECK(json["updated_at"] == 20);
}
