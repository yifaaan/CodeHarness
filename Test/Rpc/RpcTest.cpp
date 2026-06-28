#include "Rpc/CoreApi.h"
#include "Rpc/RpcTypes.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Engine/LoopTypes.h"
#include "Host/LocalHost.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Permission/PermissionTypes.h"
#include "Session/SessionTypes.h"
#include "absl/status/status.h"

namespace rpc = codeharness::rpc;
namespace agent = codeharness::agent;
namespace engine = codeharness::engine;
namespace host = codeharness::host;
namespace llm = codeharness::llm;
namespace permission = codeharness::permission;

namespace
{

	struct MockToolCall
	{
		int index = 0;
		std::string id = "call_1";
		std::string name;
		std::string arguments;
	};

	struct CannedResponse
	{
		std::string text;
		std::vector<MockToolCall> toolCalls;
		llm::FinishReason finish = llm::FinishReason::Completed;
		llm::TokenUsage usage{};
	};

	class MockChatProvider : public llm::ChatProvider
	{
	public:
		std::vector<CannedResponse> responses;
		std::vector<std::vector<llm::Message>> histories;
		size_t callCount = 0;

		std::string Name() const override
		{
			return "mock";
		}
		std::string ModelName() const override
		{
			return "mock-model";
		}
		std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override
		{
			return std::nullopt;
		}

		absl::Status Generate(std::string_view, std::span<const llm::Tool>, std::span<const llm::Message> messages,
							  const llm::StreamCallbacks& callbacks, std::stop_token = {}) override
		{
			histories.emplace_back(messages.begin(), messages.end());
			size_t idx = callCount++;
			if (idx >= responses.size())
			{
				return absl::InternalError("no more canned responses");
			}

			const auto& response = responses[idx];
			if (!response.text.empty() && callbacks.onText)
			{
				callbacks.onText(response.text);
			}
			for (const auto& toolCall : response.toolCalls)
			{
				if (callbacks.onToolCallStart)
				{
					callbacks.onToolCallStart(toolCall.index, toolCall.id, toolCall.name);
				}
				if (!toolCall.arguments.empty() && callbacks.onToolCallDelta)
				{
					callbacks.onToolCallDelta(toolCall.index, toolCall.arguments);
				}
			}
			if (callbacks.onFinish)
			{
				callbacks.onFinish(response.finish, response.usage);
			}
			return absl::OkStatus();
		}
	};

	struct TmpDirFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;

		TmpDirFixture()
		{
			auto tmpBase = std::filesystem::temp_directory_path();
			auto now = std::chrono::steady_clock::now().time_since_epoch().count();
			tmpDir = tmpBase / ("codeharness_rpc_test_" + std::to_string(now));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
		}

		~TmpDirFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}
	};

	rpc::CoreApi MakeCoreApi(host::LocalHost& h, MockChatProvider& provider,
							 std::vector<rpc::CoreEvent>* events = nullptr,
							 permission::ApprovalCallback approval = {})
	{
		rpc::CoreApiConfig cfg;
		cfg.host = &h;
		cfg.providerResolver = [&provider](std::string_view) -> absl::StatusOr<std::pair<llm::ChatProvider*, std::string>> {
			return std::make_pair(static_cast<llm::ChatProvider*>(&provider), std::string("mock-model"));
		};
		if (events != nullptr)
		{
			cfg.eventSink = [events](const rpc::CoreEvent& event) { events->push_back(event); };
		}
		cfg.approvalCallback = std::move(approval);
		return rpc::CoreApi(std::move(cfg));
	}

	rpc::CreateSessionOptions ManualSession(std::string workdir)
	{
		return {
			.workdir = std::move(workdir),
			.title = "rpc-test",
			.permissionMode = codeharness::config::PermissionMode::Manual,
		};
	}

	rpc::CreateSessionOptions YoloSession(std::string workdir)
	{
		return {
			.workdir = std::move(workdir),
			.title = "rpc-test",
			.permissionMode = codeharness::config::PermissionMode::Yolo,
		};
	}

} // namespace

TEST_CASE("CoreApi: create prompt close succeeds")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "hello"}};
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());

	auto result = api.Prompt(*sessionId, "hi");
	REQUIRE(result.ok());
	CHECK(result->stopReason == engine::StopReason::Completed);
	CHECK(provider.callCount == 1);

	CHECK(api.CloseSession(*sessionId).ok());
}

TEST_CASE("CoreApi: resume replays prior history")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "first"}, {.text = "second"}};
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	REQUIRE(api.Prompt(*sessionId, "one").ok());
	REQUIRE(api.CloseSession(*sessionId).ok());

	auto resumed = api.ResumeSession(*sessionId, YoloSession(f.tmpDir.string()));
	REQUIRE(resumed.ok());
	CHECK(*resumed == *sessionId);
	REQUIRE(api.Prompt(*resumed, "two").ok());

	REQUIRE(provider.histories.size() >= 2);
	CHECK(provider.histories.back().size() >= 2);
}

TEST_CASE("CoreApi: ListSessions returns created sessions")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "hello"}};
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());

	auto sessions = api.ListSessions(f.tmpDir.string());
	REQUIRE(sessions.ok());
	REQUIRE(sessions->size() == 1);
	CHECK(sessions->front().sessionId == *sessionId);
}

TEST_CASE("CoreApi: RemoveSession deletes active stored session")
{
	TmpDirFixture f;
	MockChatProvider provider;
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	CHECK(api.RemoveSession(*sessionId).ok());

	auto sessions = api.ListSessions(f.tmpDir.string());
	REQUIRE(sessions.ok());
	CHECK(sessions->empty());

	auto info = api.GetSessionInfo(*sessionId);
	CHECK_FALSE(info.ok());
	CHECK(absl::IsNotFound(info.status()));
}

TEST_CASE("CoreApi: prompt emits turn and assistant events")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "events"}};
	std::vector<rpc::CoreEvent> events;
	auto api = MakeCoreApi(f.host, provider, &events);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	REQUIRE(api.Prompt(*sessionId, "go").ok());

	bool sawStarted = false;
	bool sawDelta = false;
	bool sawEnded = false;
	for (const auto& event : events)
	{
		sawStarted = sawStarted || std::holds_alternative<agent::TurnStartedEvent>(event.event);
		sawEnded = sawEnded || std::holds_alternative<agent::TurnEndedEvent>(event.event);
		if (auto* loop = std::get_if<agent::LoopEvent>(&event.event))
		{
			sawDelta = sawDelta || std::holds_alternative<engine::AssistantDeltaEvent>(loop->event);
		}
		CHECK(event.sessionId == *sessionId);
		CHECK(event.agentId == "main");
	}
	CHECK(sawStarted);
	CHECK(sawDelta);
	CHECK(sawEnded);
}

TEST_CASE("CoreApi: manual permission callback allows mutating tools")
{
	TmpDirFixture f;
	MockChatProvider provider;
	auto outPath = (f.tmpDir / "allowed.txt").string();
	provider.responses = {
		{.toolCalls = {{.name = "Write",
						.arguments = nlohmann::json({{"path", outPath}, {"content", "ok"}}).dump()}},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done"},
	};
	int approvals = 0;
	auto api = MakeCoreApi(f.host, provider, nullptr,
						   [&approvals](std::string_view, const nlohmann::json&, std::string_view) {
							   ++approvals;
							   return permission::PermissionDecision::Allow;
						   });

	auto sessionId = api.CreateSession(ManualSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	REQUIRE(api.Prompt(*sessionId, "write it").ok());

	CHECK(approvals == 1);
	CHECK(std::filesystem::exists(outPath));
}

TEST_CASE("CoreApi: manual permission callback denies mutating tools")
{
	TmpDirFixture f;
	MockChatProvider provider;
	auto outPath = (f.tmpDir / "denied.txt").string();
	provider.responses = {
		{.toolCalls = {{.name = "Write",
						.arguments = nlohmann::json({{"path", outPath}, {"content", "no"}}).dump()}},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done"},
	};
	std::vector<rpc::CoreEvent> events;
	auto api = MakeCoreApi(f.host, provider, &events,
						   [](std::string_view, const nlohmann::json&, std::string_view) {
							   return permission::PermissionDecision::Deny;
						   });

	auto sessionId = api.CreateSession(ManualSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	REQUIRE(api.Prompt(*sessionId, "write it").ok());

	bool sawDenied = false;
	for (const auto& event : events)
	{
		if (auto* loop = std::get_if<agent::LoopEvent>(&event.event))
		{
			sawDenied = sawDenied || std::holds_alternative<engine::PermissionDeniedEvent>(loop->event);
		}
	}
	CHECK(sawDenied);
	CHECK_FALSE(std::filesystem::exists(outPath));
}

TEST_CASE("CoreApi: ActivateSkill reports missing skill without crashing")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "hello"}};
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());

	auto status = api.ActivateSkill(*sessionId, "missing-skill", "");
	CHECK_FALSE(status.ok());
}

TEST_CASE("CoreApi: close is idempotent and prompt after close fails")
{
	TmpDirFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "hello"}};
	auto api = MakeCoreApi(f.host, provider);

	auto sessionId = api.CreateSession(YoloSession(f.tmpDir.string()));
	REQUIRE(sessionId.ok());
	CHECK(api.CloseSession(*sessionId).ok());
	CHECK(api.CloseSession(*sessionId).ok());

	auto result = api.Prompt(*sessionId, "after close");
	CHECK_FALSE(result.ok());
	CHECK(result.status().code() == absl::StatusCode::kFailedPrecondition);
}
