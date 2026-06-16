#include "Cli/RunPrompt.h"

#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "Agent/AgentTypes.h"
#include "Engine/LoopTypes.h"
#include "Llm/ChatProvider.h"
#include "Permission/PermissionTypes.h"
#include "Rpc/CoreApi.h"
#include "Rpc/RpcTypes.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::cli
{

	namespace
	{

		struct SkillOption
		{
			std::string name;
			std::string args;
		};

		SkillOption ParseSkillOption(const std::string& skill)
		{
			auto pos = skill.find(':');
			if (pos == std::string::npos)
			{
				return {skill, ""};
			}
			return {skill.substr(0, pos), skill.substr(pos + 1)};
		}

		std::string Trim(std::string_view value)
		{
			auto begin = value.find_first_not_of(" \t\r\n");
			if (begin == std::string_view::npos)
			{
				return "";
			}
			auto end = value.find_last_not_of(" \t\r\n");
			return std::string(value.substr(begin, end - begin + 1));
		}

		void PrintJsonLine(nlohmann::json obj)
		{
			std::cout << obj.dump() << '\n';
			std::cout.flush();
		}

		permission::ApprovalCallback MakeStdinApprovalCallback(std::istream* input)
		{
			return [input](std::string_view toolName, const nlohmann::json& args, std::string_view description) {
				auto& in = input != nullptr ? *input : std::cin;
				fmt::print(stderr, "\n[permission] tool '{}' requested: {}\n  args: {}\nAllow? [y/N] ",
						   toolName, description, args.dump());
				std::cout.flush();
				std::string line;
				if (!std::getline(in, line))
				{
					fmt::print(stderr, "(EOF - denying)\n");
					return permission::PermissionDecision::Deny;
				}
				return (!line.empty() && (line[0] == 'y' || line[0] == 'Y'))
						   ? permission::PermissionDecision::Allow
						   : permission::PermissionDecision::Deny;
			};
		}

		void HandleCoreEventOutput(const rpc::CoreEvent& coreEvent, OutputFormat format)
		{
			if (format == OutputFormat::Text)
			{
				if (auto* loop = std::get_if<agent::LoopEvent>(&coreEvent.event))
				{
					if (auto* delta = std::get_if<engine::AssistantDeltaEvent>(&loop->event))
					{
						fmt::print("{}", delta->text);
						std::cout.flush();
					}
					else if (auto* denied = std::get_if<engine::PermissionDeniedEvent>(&loop->event))
					{
						fmt::print(stderr, "\n[permission denied: {}]\n", denied->toolName);
					}
				}
				else if (auto* err = std::get_if<agent::ErrorEvent>(&coreEvent.event))
				{
					fmt::print(stderr, "\n[error] {}\n", err->message);
				}
				return;
			}

			auto obj = rpc::CoreEventToJson(coreEvent);
			if (!obj.is_null())
			{
				PrintJsonLine(std::move(obj));
			}
		}

		rpc::CoreApiConfig MakeCoreConfig(const CliOptions& opts, RunDeps deps)
		{
			rpc::CoreApiConfig cfg;
			cfg.host = deps.host;
			cfg.http = deps.http;
			cfg.eventSink = [format = opts.outputFormat](const rpc::CoreEvent& event) {
				HandleCoreEventOutput(event, format);
			};
			if (deps.resolveProvider)
			{
				auto resolver = deps.resolveProvider;
				cfg.providerResolver = [resolver](std::string_view) { return resolver(); };
			}
			if (!opts.yolo)
			{
				cfg.approvalCallback = MakeStdinApprovalCallback(deps.input);
			}
			return cfg;
		}

		rpc::CreateSessionOptions MakeSessionOptions(const CliOptions& opts, std::string title)
		{
			return {
				.workdir = opts.workdir,
				.title = std::move(title),
				.model = opts.model,
				.permissionMode = opts.yolo ? config::PermissionMode::Yolo : config::PermissionMode::Manual,
			};
		}

		absl::Status ActivateStartupSkill(rpc::CoreApi& api, std::string_view sessionId, const CliOptions& opts)
		{
			if (opts.skill.empty())
			{
				return absl::OkStatus();
			}

			auto parsed = ParseSkillOption(opts.skill);
			spdlog::info("cli: activating skill '{}' with args '{}'", parsed.name, parsed.args);
			auto skillStatus = api.ActivateSkill(sessionId, parsed.name, parsed.args);
			if (!skillStatus.ok())
			{
				spdlog::warn("cli: skill activation failed: {}", skillStatus.message());
			}
			return absl::OkStatus();
		}

		absl::StatusOr<std::string> LatestSessionId(rpc::CoreApi& api, std::string_view workdir)
		{
			auto sessions = api.ListSessions(workdir);
			if (!sessions.ok())
			{
				return sessions.status();
			}
			if (sessions->empty())
			{
				return absl::NotFoundError("no sessions found for this workdir");
			}
			auto latest = std::max_element(
				sessions->begin(), sessions->end(), [](const session::SessionInfo& a, const session::SessionInfo& b) {
					if (a.updatedAt != b.updatedAt)
					{
						return a.updatedAt < b.updatedAt;
					}
					return a.createdAt < b.createdAt;
				});
			return latest->sessionId;
		}

		absl::StatusOr<std::string> OpenShellSession(rpc::CoreApi& api, const CliOptions& opts)
		{
			if (!opts.sessionId.empty())
			{
				return api.ResumeSession(opts.sessionId, MakeSessionOptions(opts, "shell"));
			}
			if (opts.continueLast)
			{
				auto sessionId = LatestSessionId(api, opts.workdir);
				if (!sessionId.ok())
				{
					return sessionId.status();
				}
				return api.ResumeSession(*sessionId, MakeSessionOptions(opts, "shell"));
			}
			return api.CreateSession(MakeSessionOptions(opts, "shell"));
		}

		void PrintShellHelp()
		{
			fmt::print(stderr,
					   "Commands:\n"
					   "  /help              Show this help\n"
					   "  /clear             Clear in-memory context\n"
					   "  /skill name[:args] Activate a skill\n"
					   "  /exit, /quit       Close the session and exit\n");
		}

		absl::Status RunPromptMode(const CliOptions& opts, rpc::CoreApi& api)
		{
			auto sessionId = api.CreateSession(MakeSessionOptions(opts, "cli"));
			if (!sessionId.ok())
			{
				return sessionId.status();
			}

			auto skillStatus = ActivateStartupSkill(api, *sessionId, opts);
			if (!skillStatus.ok())
			{
				return skillStatus;
			}

			auto result = api.Prompt(*sessionId, opts.prompt);
			if (!result.ok())
			{
				auto closeStatus = api.CloseSession(*sessionId);
				if (!closeStatus.ok())
				{
					spdlog::warn("cli: session close failed after prompt error: {}", closeStatus.message());
				}
				return result.status();
			}

			if (opts.outputFormat == OutputFormat::Text)
			{
				fmt::print("\n");
				std::cout.flush();
			}

			auto closeStatus = api.CloseSession(*sessionId);
			if (!closeStatus.ok())
			{
				spdlog::warn("cli: session close failed: {}", closeStatus.message());
			}
			return absl::OkStatus();
		}

		absl::Status RunShellMode(const CliOptions& opts, RunDeps deps, rpc::CoreApi& api)
		{
			auto sessionId = OpenShellSession(api, opts);
			if (!sessionId.ok())
			{
				return sessionId.status();
			}

			auto skillStatus = ActivateStartupSkill(api, *sessionId, opts);
			if (!skillStatus.ok())
			{
				return skillStatus;
			}

			auto& input = deps.input != nullptr ? *deps.input : std::cin;
			if (opts.outputFormat == OutputFormat::Text)
			{
				fmt::print(stderr, "codeharness shell session {}\n", *sessionId);
				PrintShellHelp();
			}

			std::string line;
			while (true)
			{
				fmt::print(stderr, "codeharness> ");
				if (!std::getline(input, line))
				{
					break;
				}

				auto text = Trim(line);
				if (text.empty())
				{
					continue;
				}
				if (text == "/exit" || text == "/quit")
				{
					break;
				}
				if (text == "/help")
				{
					PrintShellHelp();
					continue;
				}
				if (text == "/clear")
				{
					auto clearStatus = api.ClearContext(*sessionId);
					if (!clearStatus.ok())
					{
						return clearStatus;
					}
					if (opts.outputFormat == OutputFormat::StreamJson)
					{
						PrintJsonLine({{"event", "context_cleared"}, {"session_id", *sessionId}, {"agent_id", "main"}});
					}
					else
					{
						fmt::print(stderr, "context cleared\n");
					}
					continue;
				}
				if (text.rfind("/skill", 0) == 0)
				{
					auto arg = Trim(std::string_view(text).substr(6));
					if (arg.empty())
					{
						fmt::print(stderr, "usage: /skill name[:args]\n");
						continue;
					}
					auto parsed = ParseSkillOption(arg);
					auto status = api.ActivateSkill(*sessionId, parsed.name, parsed.args);
					if (!status.ok())
					{
						fmt::print(stderr, "skill activation failed: {}\n", status.message());
					}
					continue;
				}

				auto result = api.Prompt(*sessionId, text);
				if (!result.ok())
				{
					auto closeStatus = api.CloseSession(*sessionId);
					if (!closeStatus.ok())
					{
						spdlog::warn("cli: session close failed after prompt error: {}", closeStatus.message());
					}
					return result.status();
				}
				if (opts.outputFormat == OutputFormat::Text)
				{
					fmt::print("\n");
					std::cout.flush();
				}
			}

			auto closeStatus = api.CloseSession(*sessionId);
			if (!closeStatus.ok())
			{
				spdlog::warn("cli: session close failed: {}", closeStatus.message());
				return closeStatus;
			}
			return absl::OkStatus();
		}

	} // namespace

	absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
	ResolveProviderFromConfig(host::Host* host, llm::HttpClient* http, std::string_view modelOverride)
	{
		return rpc::ResolveProviderFromConfig(host, http, modelOverride);
	}

	absl::Status Run(const CliOptions& opts, RunDeps deps)
	{
		rpc::CoreApi api(MakeCoreConfig(opts, deps));
		if (opts.mode == CliMode::Shell)
		{
			return RunShellMode(opts, deps, api);
		}
		return RunPromptMode(opts, api);
	}

} // namespace codeharness::cli
