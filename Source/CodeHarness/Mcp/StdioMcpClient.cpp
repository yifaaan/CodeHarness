#include "Mcp/StdioMcpClient.h"

#include <absl/status/status.h>
#include <fmt/format.h>

#include <chrono>
#include <utility>

namespace codeharness::mcp
{
	namespace
	{

		constexpr int RequestTimeoutMs = 5000;
		constexpr int DrainSliceMs = 100;

		std::vector<std::pair<std::string, std::string>> EnvVector(const std::map<std::string, std::string>& env)
		{
			std::vector<std::pair<std::string, std::string>> out;
			out.reserve(env.size());
			for (const auto& [k, v] : env)
				out.emplace_back(k, v);
			return out;
		}

		std::string JsonContentToText(const nlohmann::json& content)
		{
			if (!content.is_array())
				return {};

			std::string out;
			for (const auto& item : content)
			{
				if (!item.is_object())
					continue;
				const auto type = item.value("type", std::string{});
				if (type == "text")
				{
					if (!out.empty() && out.back() != '\n')
						out += '\n';
					out += item.value("text", std::string{});
				}
				else
				{
					if (!out.empty() && out.back() != '\n')
						out += '\n';
					out += item.dump();
				}
			}
			return out;
		}

	} // namespace

	StdioMcpClient::StdioMcpClient(host::Host* hostPtr, McpServerConfig serverConfig)
		: host(hostPtr), config(std::move(serverConfig))
	{
	}

	StdioMcpClient::~StdioMcpClient()
	{
		(void)Shutdown();
	}

	absl::Status StdioMcpClient::EnsureStarted()
	{
		if (process)
			return absl::OkStatus();
		if (host == nullptr)
			return absl::FailedPreconditionError("no host available for MCP server");
		if (config.command.empty())
			return absl::InvalidArgumentError(fmt::format("MCP server '{}' has no command", config.name));

		std::vector<std::string> argv;
		argv.reserve(config.args.size() + 1);
		argv.push_back(config.command);
		argv.insert(argv.end(), config.args.begin(), config.args.end());

		auto started = host->ExecWithEnv(std::move(argv), config.cwd, EnvVector(config.env));
		if (!started.ok())
			return started.status();
		process = std::move(*started);
		return absl::OkStatus();
	}

	absl::Status StdioMcpClient::Send(const nlohmann::json& message)
	{
		if (auto st = EnsureStarted(); !st.ok())
			return st;
		std::string line = message.dump();
		line += '\n';
		return process->WriteStdin(line);
	}

	absl::StatusOr<nlohmann::json> StdioMcpClient::Request(std::string_view method, nlohmann::json params, std::stop_token stopToken)
	{
		const int id = nextId++;
		nlohmann::json message{
			{"jsonrpc", "2.0"},
			{"id", id},
			{"method", std::string(method)},
		};
		if (!params.is_null())
			message["params"] = std::move(params);

		if (auto st = Send(message); !st.ok())
			return st;
		return WaitForResponse(id, stopToken);
	}

	absl::StatusOr<nlohmann::json> StdioMcpClient::WaitForResponse(int id, std::stop_token stopToken)
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RequestTimeoutMs);

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (stopToken.stop_requested())
				return absl::CancelledError("MCP request cancelled");

			auto drain = process->Drain(DrainSliceMs, stopToken);
			if (!drain.ok())
				return drain.status();
			stdoutBuffer += drain->out;
			stderrBuffer += drain->err;
			if (drain->finished)
			{
				shutdown = true;
			}

			std::size_t start = 0;
			for (;;)
			{
				auto newline = stdoutBuffer.find('\n', start);
				if (newline == std::string::npos)
					break;
				std::string line = stdoutBuffer.substr(start, newline - start);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				start = newline + 1;
				if (line.empty())
					continue;

				nlohmann::json msg;
				try
				{
					msg = nlohmann::json::parse(line);
				}
				catch (const nlohmann::json::parse_error& e)
				{
					return absl::InvalidArgumentError(fmt::format("MCP server '{}' returned malformed JSON: {}", config.name, e.what()));
				}

				if (!msg.contains("id") || !msg["id"].is_number_integer() || msg["id"].get<int>() != id)
					continue;

				stdoutBuffer.erase(0, start);
				if (msg.contains("error"))
				{
					return absl::InternalError(fmt::format("MCP server '{}' error response: {}", config.name, msg["error"].dump()));
				}
				if (!msg.contains("result"))
					return absl::InvalidArgumentError(fmt::format("MCP server '{}' response missing result", config.name));
				return msg["result"];
			}
			if (start > 0)
				stdoutBuffer.erase(0, start);

			if (shutdown)
			{
				std::string suffix = stderrBuffer.empty() ? std::string{} : fmt::format("; stderr: {}", stderrBuffer);
				return absl::UnavailableError(fmt::format("MCP server '{}' exited before response{}", config.name, suffix));
			}
		}

		std::string suffix = stderrBuffer.empty() ? std::string{} : fmt::format("; stderr: {}", stderrBuffer);
		return absl::DeadlineExceededError(fmt::format("MCP server '{}' request timed out{}", config.name, suffix));
	}

	absl::Status StdioMcpClient::Initialize(std::stop_token stopToken)
	{
		if (initialized)
			return absl::OkStatus();

		nlohmann::json params{
			{"protocolVersion", "2024-11-05"},
			{"capabilities", nlohmann::json::object()},
			{"clientInfo", {{"name", "CodeHarness"}, {"version", "0.1.0"}}},
		};
		auto result = Request("initialize", std::move(params), stopToken);
		if (!result.ok())
			return result.status();

		nlohmann::json notification{
			{"jsonrpc", "2.0"},
			{"method", "notifications/initialized"},
			{"params", nlohmann::json::object()},
		};
		if (auto st = Send(notification); !st.ok())
			return st;
		initialized = true;
		return absl::OkStatus();
	}

	absl::StatusOr<std::vector<McpToolDefinition>> StdioMcpClient::ListTools(std::stop_token stopToken)
	{
		if (auto st = Initialize(stopToken); !st.ok())
			return st;

		auto result = Request("tools/list", nlohmann::json::object(), stopToken);
		if (!result.ok())
			return result.status();

		std::vector<McpToolDefinition> tools;
		const auto toolsJson = (*result).find("tools");
		if (toolsJson == result->end() || !toolsJson->is_array())
			return absl::InvalidArgumentError(fmt::format("MCP server '{}' tools/list missing tools array", config.name));

		for (const auto& entry : *toolsJson)
		{
			if (!entry.is_object())
				continue;
			McpToolDefinition def;
			def.name = entry.value("name", std::string{});
			if (def.name.empty())
				continue;
			def.description = entry.value("description", std::string{});
			if (entry.contains("inputSchema") && entry["inputSchema"].is_object())
				def.inputSchema = entry["inputSchema"];
			tools.push_back(std::move(def));
		}
		return tools;
	}

	absl::StatusOr<McpToolResult> StdioMcpClient::CallTool(std::string_view name, const nlohmann::json& arguments, std::stop_token stopToken)
	{
		if (auto st = Initialize(stopToken); !st.ok())
			return st;

		nlohmann::json params{
			{"name", std::string(name)},
			{"arguments", arguments.is_object() ? arguments : nlohmann::json::object()},
		};
		auto result = Request("tools/call", std::move(params), stopToken);
		if (!result.ok())
			return result.status();

		McpToolResult out;
		out.isError = result->value("isError", false);
		out.content = JsonContentToText((*result)["content"]);
		if (out.content.empty())
			out.content = result->dump();
		return out;
	}

	absl::Status StdioMcpClient::Shutdown()
	{
		if (!process)
			return absl::OkStatus();
		if (!shutdown)
		{
			(void)process->CloseStdin();
			(void)process->Kill("SIGTERM");
			shutdown = true;
		}
		process.reset();
		return absl::OkStatus();
	}

} // namespace codeharness::mcp
