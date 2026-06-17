#include <doctest/doctest.h>

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Config/ConfigTypes.h"
#include "Engine/Loop.h"
#include "Engine/LoopTypes.h"
#include "Engine/Tool.h"
#include "Host/HostProcess.h"
#include "Host/LocalHost.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Mcp/McpConnectionManager.h"
#include "Mcp/McpExecutableTool.h"
#include "Mcp/McpTypes.h"
#include "Mcp/StdioMcpClient.h"
#include "Permission/PermissionGate.h"
#include "Tools/ToolManager.h"

namespace host = codeharness::host;
namespace mcp = codeharness::mcp;
namespace tools = codeharness::tools;
namespace engine = codeharness::engine;
namespace permission = codeharness::permission;
namespace llm = codeharness::llm;
using json = nlohmann::json;

namespace
{

	enum class FakeMode
	{
		Normal,
		Malformed,
		ExitImmediately,
	};

	class FakeMcpProcess : public host::HostProcess
	{
	public:
		explicit FakeMcpProcess(FakeMode mode) : mode(mode) {}

		absl::Status WriteStdin(std::string_view data) override
		{
			if (exited)
				return absl::InternalError("process exited");
			buffer += data;
			for (;;)
			{
				auto newline = buffer.find('\n');
				if (newline == std::string::npos)
					break;
				auto line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				HandleLine(line);
			}
			return absl::OkStatus();
		}

		absl::Status CloseStdin() override
		{
			stdinClosed = true;
			return absl::OkStatus();
		}

		absl::StatusOr<std::string> ReadStdout() override
		{
			auto out = stdoutQueue;
			stdoutQueue.clear();
			return out;
		}

		absl::StatusOr<std::string> ReadStderr() override
		{
			auto err = stderrQueue;
			stderrQueue.clear();
			return err;
		}

		absl::StatusOr<int> Pid() const override
		{
			return 123;
		}
		absl::StatusOr<int> ExitCode() const override
		{
			return exited ? exitCode : -1;
		}
		absl::StatusOr<int> Wait() override
		{
			return exitCode;
		}

		absl::Status Kill(const std::string& = "SIGTERM") override
		{
			exited = true;
			return absl::OkStatus();
		}

		absl::StatusOr<host::DrainResult> Drain(int, std::stop_token) override
		{
			host::DrainResult result;
			if (mode == FakeMode::ExitImmediately)
			{
				exited = true;
				result.finished = true;
				result.exitCode = 9;
				return result;
			}
			result.out = std::move(stdoutQueue);
			result.err = std::move(stderrQueue);
			stdoutQueue.clear();
			stderrQueue.clear();
			result.finished = exited;
			result.exitCode = exitCode;
			return result;
		}

		bool stdinClosed = false;

	private:
		void QueueResponse(int id, json result)
		{
			stdoutQueue += json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}.dump();
			stdoutQueue += '\n';
		}

		void QueueError(int id, std::string message)
		{
			stdoutQueue += json{{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32602}, {"message", std::move(message)}}}}.dump();
			stdoutQueue += '\n';
		}

		void HandleLine(const std::string& line)
		{
			if (mode == FakeMode::Malformed)
			{
				stdoutQueue += "{not json\n";
				return;
			}

			json request = json::parse(line);
			if (!request.contains("id"))
				return; // notification
			const int id = request["id"].get<int>();
			const auto method = request.value("method", std::string{});
			if (method == "initialize")
			{
				QueueResponse(id, json{{"protocolVersion", "2024-11-05"}, {"capabilities", json::object()}});
			}
			else if (method == "tools/list")
			{
				QueueResponse(id, json{{"tools",
										json::array({json{{"name", "echo"},
														  {"description", "Echo a message"},
														  {"inputSchema",
														   json{{"type", "object"},
																{"properties", {{"msg", {{"type", "string"}}}}},
																{"required", json::array({"msg"})}}}}})}});
			}
			else if (method == "tools/call")
			{
				auto params = request["params"];
				auto name = params.value("name", std::string{});
				if (name != "echo")
				{
					QueueError(id, "unknown tool");
					return;
				}
				auto msg = params["arguments"].value("msg", std::string{});
				QueueResponse(id, json{{"content", json::array({json{{"type", "text"}, {"text", "echo: " + msg}}})}, {"isError", false}});
			}
			else
			{
				QueueError(id, "unknown method");
			}
		}

		FakeMode mode;
		std::string buffer;
		std::string stdoutQueue;
		std::string stderrQueue;
		bool exited = false;
		int exitCode = 0;
	};

	class FakeHost : public host::LocalHost
	{
	public:
		explicit FakeHost(FakeMode mode = FakeMode::Normal) : mode(mode) {}

		absl::StatusOr<std::unique_ptr<host::HostProcess>> ExecWithEnv(
			std::vector<std::string> args,
			std::string_view cwd = "",
			const std::vector<std::pair<std::string, std::string>>& env = {}) override
		{
			lastArgs = std::move(args);
			lastCwd = std::string(cwd);
			lastEnv = env;
			++spawnCount;
			return std::make_unique<FakeMcpProcess>(mode);
		}

		FakeMode mode;
		int spawnCount = 0;
		std::vector<std::string> lastArgs;
		std::string lastCwd;
		std::vector<std::pair<std::string, std::string>> lastEnv;
	};

	class OneToolCallProvider : public llm::ChatProvider
	{
	public:
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

		absl::Status Generate(
			std::string_view,
			std::span<const llm::Tool>,
			std::span<const llm::Message>,
			const llm::StreamCallbacks& callbacks,
			std::stop_token = {}) override
		{
			++calls;
			if (calls == 1)
			{
				callbacks.onToolCallStart(0, "call_1", "mcp__test__echo");
				callbacks.onToolCallDelta(0, R"({"msg":"loop"})");
				callbacks.onFinish(llm::FinishReason::ToolCalls, {});
			}
			else
			{
				callbacks.onText("done");
				callbacks.onFinish(llm::FinishReason::Completed, {});
			}
			return absl::OkStatus();
		}

		int calls = 0;
	};

	mcp::McpServerConfig Server()
	{
		mcp::McpServerConfig server;
		server.name = "test";
		server.command = "fake-mcp";
		server.args = {"--stdio"};
		server.cwd = "work";
		server.env["TOKEN"] = "abc";
		return server;
	}

} // namespace

TEST_CASE("StdioMcpClient: initializes and lists tools")
{
	FakeHost host;
	mcp::StdioMcpClient client(&host, Server());

	auto tools = client.ListTools();
	REQUIRE(tools.ok());
	REQUIRE(tools->size() == 1);
	CHECK((*tools)[0].name == "echo");
	CHECK((*tools)[0].description == "Echo a message");
	CHECK((*tools)[0].inputSchema["type"] == "object");
	REQUIRE(host.spawnCount == 1);
	CHECK(host.lastArgs == std::vector<std::string>{"fake-mcp", "--stdio"});
	CHECK(host.lastCwd == "work");
	REQUIRE(host.lastEnv.size() == 1);
	CHECK(host.lastEnv[0].first == "TOKEN");
	CHECK(host.lastEnv[0].second == "abc");
}

TEST_CASE("StdioMcpClient: calls a remote tool")
{
	FakeHost host;
	mcp::StdioMcpClient client(&host, Server());

	auto result = client.CallTool("echo", json{{"msg", "hello"}});
	REQUIRE(result.ok());
	CHECK(result->content == "echo: hello");
	CHECK_FALSE(result->isError);
}

TEST_CASE("StdioMcpClient: malformed JSON returns an error")
{
	FakeHost host(FakeMode::Malformed);
	mcp::StdioMcpClient client(&host, Server());

	auto result = client.ListTools();
	CHECK_FALSE(result.ok());
	CHECK(result.status().code() == absl::StatusCode::kInvalidArgument);
}

TEST_CASE("StdioMcpClient: server exit returns an error")
{
	FakeHost host(FakeMode::ExitImmediately);
	mcp::StdioMcpClient client(&host, Server());

	auto result = client.ListTools();
	CHECK_FALSE(result.ok());
	CHECK(result.status().code() == absl::StatusCode::kUnavailable);
}

TEST_CASE("McpExecutableTool: wraps calls and requires permission")
{
	FakeHost host;
	auto client = std::make_unique<mcp::StdioMcpClient>(&host, Server());
	mcp::McpToolDefinition def;
	def.name = "echo";
	def.description = "Echo";
	def.inputSchema = json{{"type", "object"}};

	mcp::McpExecutableTool tool("mcp__test__echo", "test", def, client.get());
	auto execution = tool.ResolveExecution(json::object());
	REQUIRE(execution.ok());
	CHECK(execution->requiresPermission);

	int approvals = 0;
	permission::PermissionGate gate(
		codeharness::config::PermissionMode::Manual,
		[&approvals](std::string_view, const json&, std::string_view) {
			++approvals;
			return permission::PermissionDecision::Allow;
		});
	CHECK(gate.ShouldRun(execution->requiresPermission, tool.Name(), json::object(), execution->description));
	CHECK(approvals == 1);

	auto result = tool.Execute(json{{"msg", "from tool"}}, engine::ToolContext{.host = &host});
	REQUIRE(result.ok());
	CHECK(result->content == "echo: from tool");
	CHECK_FALSE(result->isError);
}

TEST_CASE("McpConnectionManager: registers discovered tools")
{
	FakeHost host;
	tools::ToolManager manager;
	mcp::McpConnectionManager connections(&host, {Server()});

	auto status = connections.RegisterTools(manager);
	REQUIRE(status.ok());
	CHECK(manager.Size() == 1);
	auto* tool = manager.Find("mcp__test__echo");
	REQUIRE(tool != nullptr);
	CHECK(tool->Description().find("[MCP:test]") != std::string::npos);
}

TEST_CASE("McpConnectionManager: skips failed servers")
{
	FakeHost host(FakeMode::Malformed);
	tools::ToolManager manager;
	mcp::McpConnectionManager connections(&host, {Server()});

	auto status = connections.RegisterTools(manager);
	REQUIRE(status.ok());
	CHECK(manager.Size() == 0);
}

TEST_CASE("Loop: MCP tool is permission gated and executable")
{
	FakeHost host;
	tools::ToolManager manager;
	mcp::McpConnectionManager connections(&host, {Server()});
	REQUIRE(connections.RegisterTools(manager).ok());
	auto loopTools = manager.LoopTools();

	OneToolCallProvider provider;
	int approvals = 0;
	permission::PermissionGate gate(
		codeharness::config::PermissionMode::Manual,
		[&approvals](std::string_view toolName, const json&, std::string_view) {
			CHECK(toolName == "mcp__test__echo");
			++approvals;
			return permission::PermissionDecision::Allow;
		});

	std::vector<engine::LoopEvent> events;
	engine::TurnInput input{
		.provider = &provider,
		.tools = loopTools,
		.host = &host,
		.dispatchEvent = [&events](const engine::LoopEvent& ev) { events.push_back(ev); },
		.permissionGate = &gate,
	};
	auto result = engine::RunTurn(std::move(input));
	REQUIRE(result.stopReason == engine::StopReason::Completed);
	CHECK(approvals == 1);
	bool sawResult = false;
	for (const auto& ev : events)
	{
		if (const auto* toolResult = std::get_if<engine::ToolResultEvent>(&ev))
		{
			sawResult = true;
			CHECK(toolResult->name == "mcp__test__echo");
			CHECK(toolResult->result.content == "echo: loop");
			CHECK_FALSE(toolResult->result.isError);
		}
	}
	CHECK(sawResult);
}
