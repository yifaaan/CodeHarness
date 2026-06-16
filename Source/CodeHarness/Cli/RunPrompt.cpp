#include "Cli/RunPrompt.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Agent/Agent.h"
#include "Agent/AgentTypes.h"
#include "Config/Config.h"
#include "Config/ConfigManager.h"
#include "Config/ConfigTypes.h"
#include "Config/ProviderManager.h"
#include "Engine/LoopTypes.h"
#include "Host/Host.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Mcp/McpConnectionManager.h"
#include "Mcp/McpTypes.h"
#include "Permission/PermissionTypes.h"
#include "Records/RecordJson.h"
#include "Session/Session.h"
#include "Session/SessionStore.h"
#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"
#include "Skills/SkillScanner.h"
#include "Skills/SkillTool.h"
#include "Skills/SkillTypes.h"
#include "Tools/Bash.h"
#include "Tools/EditFile.h"
#include "Tools/Glob.h"
#include "Tools/Grep.h"
#include "Tools/ReadFile.h"
#include "Tools/ToolManager.h"
#include "Tools/WriteFile.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

		struct CliRuntime
		{
			std::string workdir;
			std::unique_ptr<llm::ChatProvider> ownedProvider;
			llm::ChatProvider* provider = nullptr;
			std::string modelName;
			skills::SkillRegistry skillRegistry;
			std::unique_ptr<skills::SkillManager> skillManager;
			std::unique_ptr<mcp::McpConnectionManager> mcpManager;
			std::unique_ptr<tools::ToolManager> toolManager;
			std::unique_ptr<session::SessionStore> sessionStore;
		};

		void RegisterBuiltInTools(tools::ToolManager& tm, skills::SkillManager* skillManager = nullptr)
		{
			tm.Register(std::make_unique<tools::ReadFileTool>());
			tm.Register(std::make_unique<tools::WriteFileTool>());
			tm.Register(std::make_unique<tools::EditFileTool>());
			tm.Register(std::make_unique<tools::GlobTool>());
			tm.Register(std::make_unique<tools::GrepTool>());
			tm.Register(std::make_unique<tools::BashTool>());
			if (skillManager != nullptr)
			{
				tm.Register(std::make_unique<tools::SkillTool>(skillManager));
			}
		}

		std::vector<skills::SkillRoot> ResolveSkillRoots(host::Host* host, const config::SkillConfig& skillsCfg)
		{
			std::vector<skills::SkillRoot> roots;

			if (skillsCfg.allowProjectSkills)
			{
				auto cwd = host->GetCwd();
				if (cwd.ok())
				{
					roots.push_back({*cwd + "/.agents/skills", skills::SkillSource::Project});
				}
			}

			auto home = host->GetHome();
			if (home.ok())
			{
				roots.push_back({*home + "/.agents/skills", skills::SkillSource::User});
			}

			for (const auto& dir : skillsCfg.extraSkillDirs)
			{
				if (!dir.empty())
				{
					roots.push_back({dir, skills::SkillSource::Extra});
				}
			}

			return roots;
		}

		absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
		ResolveProviderWithConfig(config::KimiConfig cfg, llm::HttpClient* http, std::string_view modelOverride)
		{
			if (cfg.providers.empty())
			{
				return absl::FailedPreconditionError(
					"no [providers] configured in config.toml. Add a provider and a [models] entry before running.");
			}

			std::string model = modelOverride.empty() ? cfg.defaultModel : std::string(modelOverride);
			if (model.empty())
			{
				return absl::FailedPreconditionError("no model specified and config has no default_model");
			}

			config::ProviderManager pm(std::move(cfg), http);
			auto resolved = pm.ResolveForModel(model);
			if (!resolved.ok())
			{
				return resolved.status();
			}
			return std::make_pair(std::move(resolved->provider), std::move(resolved->modelName));
		}

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

		const char* StopReasonName(engine::StopReason reason)
		{
			switch (reason)
			{
			case engine::StopReason::Completed:
				return "completed";
			case engine::StopReason::MaxSteps:
				return "max_steps";
			case engine::StopReason::Aborted:
				return "aborted";
			case engine::StopReason::Error:
				return "error";
			}
			return "unknown";
		}

		const char* AgentStatusName(agent::AgentStatus status)
		{
			switch (status)
			{
			case agent::AgentStatus::Idle:
				return "idle";
			case agent::AgentStatus::Running:
				return "running";
			case agent::AgentStatus::Cancelling:
				return "cancelling";
			}
			return "unknown";
		}

		nlohmann::json TokenUsageToJson(const llm::TokenUsage& usage)
		{
			return {
				{"input_other", usage.inputOther},
				{"output", usage.output},
				{"input_cache_read", usage.inputCacheRead},
				{"input_cache_creation", usage.inputCacheCreation},
			};
		}

		nlohmann::json PromptResultToJson(const agent::PromptResult& result)
		{
			return {
				{"turn_id", result.turnId},
				{"stop_reason", StopReasonName(result.stopReason)},
				{"steps_executed", result.stepsExecuted},
				{"usage", TokenUsageToJson(result.usage)},
				{"error_message", result.errorMessage},
			};
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

		void WireEventOutput(agent::Agent& agent, OutputFormat format)
		{
			agent.SetEventDispatcher([format](const agent::AgentEvent& ev) {
				if (format == OutputFormat::Text)
				{
					if (auto* loop = std::get_if<agent::LoopEvent>(&ev))
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
					else if (auto* err = std::get_if<agent::ErrorEvent>(&ev))
					{
						fmt::print(stderr, "\n[error] {}\n", err->message);
					}
					return;
				}

				nlohmann::json obj;
				if (auto* started = std::get_if<agent::TurnStartedEvent>(&ev))
				{
					obj = {{"event", "turn_started"}, {"turn_id", started->turnId}};
				}
				else if (auto* loop = std::get_if<agent::LoopEvent>(&ev))
				{
					obj = {{"event", "loop"}, {"loop_event", records::LoopEventToJson(loop->event)}};
				}
				else if (auto* ended = std::get_if<agent::TurnEndedEvent>(&ev))
				{
					obj = {{"event", "turn_ended"}, {"result", PromptResultToJson(ended->result)}};
				}
				else if (auto* status = std::get_if<agent::StatusChangedEvent>(&ev))
				{
					obj = {{"event", "status_changed"}, {"status", AgentStatusName(status->status)}};
				}
				else if (auto* err = std::get_if<agent::ErrorEvent>(&ev))
				{
					obj = {{"event", "error"}, {"message", err->message}};
				}
				else if (auto* compacting = std::get_if<agent::ContextCompactingEvent>(&ev))
				{
					obj = {{"event", "context_compacting"}, {"message_count", compacting->messageCount}};
				}
				else if (auto* skill = std::get_if<agent::SkillInvokedEvent>(&ev))
				{
					obj = {{"event", "skill_invoked"}, {"skill", skill->skillName}, {"args", skill->args}};
				}
				if (!obj.is_null())
				{
					PrintJsonLine(std::move(obj));
				}
			});
		}

		absl::Status ConfigureAgent(agent::Agent& agent, skills::SkillManager& skillManager, const CliOptions& opts,
									std::istream* input)
		{
			WireEventOutput(agent, opts.outputFormat);
			agent.SetSkillManager(&skillManager);

			if (opts.yolo)
			{
				agent.SetPermissionMode(config::PermissionMode::Yolo);
			}
			else
			{
				agent.SetPermissionMode(config::PermissionMode::Manual);
				agent.SetApprovalCallback(MakeStdinApprovalCallback(input));
			}

			if (!opts.skill.empty())
			{
				auto parsed = ParseSkillOption(opts.skill);
				spdlog::info("cli: activating skill '{}' with args '{}'", parsed.name, parsed.args);
				auto skillStatus = agent.ActivateSkill(parsed.name, parsed.args);
				if (!skillStatus.ok())
				{
					spdlog::warn("cli: skill activation failed: {}", skillStatus.message());
				}
			}
			return absl::OkStatus();
		}

		absl::StatusOr<std::unique_ptr<CliRuntime>> BuildRuntime(const CliOptions& opts, RunDeps deps)
		{
			if (deps.host == nullptr)
			{
				return absl::InvalidArgumentError("RunDeps.host must be set");
			}

			auto runtime = std::make_unique<CliRuntime>();
			runtime->workdir = opts.workdir;
			if (runtime->workdir.empty())
			{
				auto cwd = deps.host->GetCwd();
				if (!cwd.ok())
				{
					return cwd.status();
				}
				runtime->workdir = *cwd;
			}
			auto chdirStatus = deps.host->Chdir(runtime->workdir);
			if (!chdirStatus.ok())
			{
				return chdirStatus;
			}

			config::SkillConfig skillCfg;
			std::vector<mcp::McpServerConfig> mcpServers;
			if (deps.resolveProvider)
			{
				auto r = deps.resolveProvider();
				if (!r.ok())
				{
					return r.status();
				}
				runtime->provider = r->first;
				runtime->modelName = r->second;
			}
			else
			{
				config::ConfigManager cm(deps.host);
				auto cfgResult = cm.Load();
				if (!cfgResult.ok())
				{
					return absl::FailedPreconditionError(
						fmt::format("failed to load config: {}. Create one at {} (see docs/guides/cli.md)",
									cfgResult.status().message(), *cm.ConfigPath()));
				}
				skillCfg = cfgResult->skills;
				mcpServers = cfgResult->mcpServers;
				auto resolved = ResolveProviderWithConfig(std::move(*cfgResult), deps.http, opts.model);
				if (!resolved.ok())
				{
					return resolved.status();
				}
				runtime->ownedProvider = std::move(resolved->first);
				runtime->provider = runtime->ownedProvider.get();
				runtime->modelName = std::move(resolved->second);
			}

			if (runtime->provider == nullptr)
			{
				return absl::InternalError("provider resolution returned null");
			}

			auto skillRoots = ResolveSkillRoots(deps.host, skillCfg);
			runtime->skillRegistry.LoadRoots(skillRoots, deps.host);
			spdlog::info("cli: loaded {} skills from {} roots", runtime->skillRegistry.Size(), skillRoots.size());
			runtime->skillManager = std::make_unique<skills::SkillManager>(&runtime->skillRegistry);

			runtime->mcpManager = std::make_unique<mcp::McpConnectionManager>(deps.host, std::move(mcpServers));
			runtime->toolManager = std::make_unique<tools::ToolManager>();
			RegisterBuiltInTools(*runtime->toolManager,
								 runtime->skillRegistry.Empty() ? nullptr : runtime->skillManager.get());
			(void)runtime->mcpManager->RegisterTools(*runtime->toolManager);

			auto root = session::SessionStore::ResolveSessionsRoot(deps.host);
			if (!root.ok())
			{
				return root.status();
			}
			runtime->sessionStore = std::make_unique<session::SessionStore>(deps.host, *root);
			return runtime;
		}

		session::SessionConfig MakeSessionConfig(host::Host* host, CliRuntime& runtime, std::string title)
		{
			return {
				.host = host,
				.provider = runtime.provider,
				.toolManager = runtime.toolManager.get(),
				.workdir = runtime.workdir,
				.title = std::move(title),
			};
		}

		absl::StatusOr<std::string> LatestSessionId(session::SessionStore& store, std::string_view workdir)
		{
			auto sessions = store.List(workdir);
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

		absl::StatusOr<std::unique_ptr<session::Session>> OpenShellSession(const CliOptions& opts, RunDeps deps,
																			CliRuntime& runtime)
		{
			if (!opts.sessionId.empty())
			{
				return session::Session::Resume(runtime.sessionStore.get(),
											   MakeSessionConfig(deps.host, runtime, "shell"), opts.sessionId);
			}
			if (opts.continueLast)
			{
				auto sessionId = LatestSessionId(*runtime.sessionStore, runtime.workdir);
				if (!sessionId.ok())
				{
					return sessionId.status();
				}
				return session::Session::Resume(runtime.sessionStore.get(),
											   MakeSessionConfig(deps.host, runtime, "shell"), *sessionId);
			}
			return session::Session::Create(runtime.sessionStore.get(), MakeSessionConfig(deps.host, runtime, "shell"));
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

		absl::Status RunPromptMode(const CliOptions& opts, RunDeps deps, CliRuntime& runtime)
		{
			auto sess = session::Session::Create(runtime.sessionStore.get(), MakeSessionConfig(deps.host, runtime, "cli"));
			if (!sess.ok())
			{
				return sess.status();
			}

			auto* agent = (*sess)->MainAgent();
			if (agent == nullptr)
			{
				return absl::InternalError("session created but has no main agent");
			}
			runtime.skillManager->SetSessionId((*sess)->Id());
			auto cfgStatus = ConfigureAgent(*agent, *runtime.skillManager, opts, deps.input);
			if (!cfgStatus.ok())
			{
				return cfgStatus;
			}

			spdlog::info("cli: prompting model '{}' in '{}'", runtime.modelName, runtime.workdir);
			auto result = agent->Prompt(opts.prompt);
			if (!result.ok())
			{
				return result.status();
			}

			if (opts.outputFormat == OutputFormat::Text)
			{
				fmt::print("\n");
				std::cout.flush();
			}

			auto closeStatus = (*sess)->Close();
			if (!closeStatus.ok())
			{
				spdlog::warn("cli: session close failed: {}", closeStatus.message());
			}
			return absl::OkStatus();
		}

		absl::Status RunShellMode(const CliOptions& opts, RunDeps deps, CliRuntime& runtime)
		{
			auto sess = OpenShellSession(opts, deps, runtime);
			if (!sess.ok())
			{
				return sess.status();
			}

			auto* agent = (*sess)->MainAgent();
			if (agent == nullptr)
			{
				return absl::InternalError("session opened but has no main agent");
			}
			runtime.skillManager->SetSessionId((*sess)->Id());
			auto cfgStatus = ConfigureAgent(*agent, *runtime.skillManager, opts, deps.input);
			if (!cfgStatus.ok())
			{
				return cfgStatus;
			}

			auto& input = deps.input != nullptr ? *deps.input : std::cin;
			if (opts.outputFormat == OutputFormat::Text)
			{
				fmt::print(stderr, "codeharness shell session {}\n", (*sess)->Id());
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
					agent->ClearContext();
					if (opts.outputFormat == OutputFormat::StreamJson)
					{
						PrintJsonLine({{"event", "context_cleared"}});
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
					auto status = agent->ActivateSkill(parsed.name, parsed.args);
					if (!status.ok())
					{
						fmt::print(stderr, "skill activation failed: {}\n", status.message());
					}
					continue;
				}

				auto result = agent->Prompt(text);
				if (!result.ok())
				{
					auto closeStatus = (*sess)->Close();
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

			auto closeStatus = (*sess)->Close();
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
		config::ConfigManager cm(host);
		auto cfgResult = cm.Load();
		if (!cfgResult.ok())
		{
			return absl::FailedPreconditionError(
				fmt::format("failed to load config: {}. Create one at {} (see docs/guides/cli.md)",
							cfgResult.status().message(), *cm.ConfigPath()));
		}
		return ResolveProviderWithConfig(std::move(*cfgResult), http, modelOverride);
	}

	absl::Status Run(const CliOptions& opts, RunDeps deps)
	{
		auto runtime = BuildRuntime(opts, deps);
		if (!runtime.ok())
		{
			return runtime.status();
		}

		absl::Status status = absl::OkStatus();
		if (opts.mode == CliMode::Shell)
		{
			status = RunShellMode(opts, deps, **runtime);
		}
		else
		{
			status = RunPromptMode(opts, deps, **runtime);
		}

		if ((*runtime)->mcpManager)
		{
			(void)(*runtime)->mcpManager->Shutdown();
		}
		return status;
	}

} // namespace codeharness::cli
