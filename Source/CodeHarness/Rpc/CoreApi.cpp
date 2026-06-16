#include "Rpc/CoreApi.h"

#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "Agent/Agent.h"
#include "Config/Config.h"
#include "Config/ConfigManager.h"
#include "Config/ProviderManager.h"
#include "Engine/LoopTypes.h"
#include "Host/Host.h"
#include "Llm/ChatProvider.h"
#include "Mcp/McpConnectionManager.h"
#include "Mcp/McpTypes.h"
#include "Records/RecordJson.h"
#include "Session/Session.h"
#include "Session/SessionStore.h"
#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"
#include "Skills/SkillScanner.h"
#include "Skills/SkillTool.h"
#include "Tools/Bash.h"
#include "Tools/EditFile.h"
#include "Tools/Glob.h"
#include "Tools/Grep.h"
#include "Tools/ReadFile.h"
#include "Tools/ToolManager.h"
#include "Tools/WriteFile.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::rpc
{

	namespace
	{

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

		void RegisterBuiltInTools(tools::ToolManager& tm, skills::SkillManager* skillManager)
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

	} // namespace

	struct CoreApi::CoreSessionRuntime
	{
		~CoreSessionRuntime()
		{
			if (session && !session->IsClosed())
			{
				(void)session->Close();
			}
			if (mcpManager)
			{
				(void)mcpManager->Shutdown();
			}
		}

		std::string workdir;
		std::string modelName;
		std::unique_ptr<llm::ChatProvider> ownedProvider;
		llm::ChatProvider* provider = nullptr;
		skills::SkillRegistry skillRegistry;
		std::unique_ptr<skills::SkillManager> skillManager;
		std::unique_ptr<mcp::McpConnectionManager> mcpManager;
		std::unique_ptr<tools::ToolManager> toolManager;
		bool closed = false;
		std::unique_ptr<session::Session> session;
	};

	namespace
	{

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

	} // namespace

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

	nlohmann::json CoreEventToJson(const CoreEvent& event)
	{
		nlohmann::json obj;
		if (auto* started = std::get_if<agent::TurnStartedEvent>(&event.event))
		{
			obj = {{"event", "turn_started"}, {"turn_id", started->turnId}};
		}
		else if (auto* loop = std::get_if<agent::LoopEvent>(&event.event))
		{
			obj = {{"event", "loop"}, {"loop_event", records::LoopEventToJson(loop->event)}};
		}
		else if (auto* ended = std::get_if<agent::TurnEndedEvent>(&event.event))
		{
			obj = {{"event", "turn_ended"}, {"result", PromptResultToJson(ended->result)}};
		}
		else if (auto* status = std::get_if<agent::StatusChangedEvent>(&event.event))
		{
			obj = {{"event", "status_changed"}, {"status", AgentStatusName(status->status)}};
		}
		else if (auto* err = std::get_if<agent::ErrorEvent>(&event.event))
		{
			obj = {{"event", "error"}, {"message", err->message}};
		}
		else if (auto* compacting = std::get_if<agent::ContextCompactingEvent>(&event.event))
		{
			obj = {{"event", "context_compacting"}, {"message_count", compacting->messageCount}};
		}
		else if (auto* skill = std::get_if<agent::SkillInvokedEvent>(&event.event))
		{
			obj = {{"event", "skill_invoked"}, {"skill", skill->skillName}, {"args", skill->args}};
		}

		if (!obj.is_null())
		{
			obj["session_id"] = event.sessionId;
			obj["agent_id"] = event.agentId;
		}
		return obj;
	}

	CoreApi::CoreApi(CoreApiConfig config) : config(std::move(config)) {}

	CoreApi::~CoreApi() = default;

	absl::Status CoreApi::EnsureSessionStore()
	{
		if (config.host == nullptr)
		{
			return absl::InvalidArgumentError("CoreApiConfig.host must be set");
		}
		if (sessionStore)
		{
			return absl::OkStatus();
		}

		auto root = session::SessionStore::ResolveSessionsRoot(config.host);
		if (!root.ok())
		{
			return root.status();
		}
		sessionStore = std::make_unique<session::SessionStore>(config.host, *root);
		return absl::OkStatus();
	}

	absl::StatusOr<std::string> CoreApi::ResolveWorkdir(std::string_view workdir)
	{
		if (config.host == nullptr)
		{
			return absl::InvalidArgumentError("CoreApiConfig.host must be set");
		}
		if (!workdir.empty())
		{
			return std::string(workdir);
		}
		auto cwd = config.host->GetCwd();
		if (!cwd.ok())
		{
			return cwd.status();
		}
		return *cwd;
	}

	absl::StatusOr<std::unique_ptr<CoreApi::CoreSessionRuntime>> CoreApi::BuildRuntime(std::string_view workdir,
																					   std::string_view modelOverride)
	{
		if (config.host == nullptr)
		{
			return absl::InvalidArgumentError("CoreApiConfig.host must be set");
		}

		auto runtime = std::make_unique<CoreSessionRuntime>();
		runtime->workdir = std::string(workdir);
		auto chdirStatus = config.host->Chdir(runtime->workdir);
		if (!chdirStatus.ok())
		{
			return chdirStatus;
		}

		config::SkillConfig skillCfg;
		std::vector<mcp::McpServerConfig> mcpServers;
		if (config.providerResolver)
		{
			auto resolved = config.providerResolver(modelOverride);
			if (!resolved.ok())
			{
				return resolved.status();
			}
			runtime->provider = resolved->first;
			runtime->modelName = resolved->second;
		}
		else
		{
			config::ConfigManager cm(config.host);
			auto cfgResult = cm.Load();
			if (!cfgResult.ok())
			{
				return absl::FailedPreconditionError(
					fmt::format("failed to load config: {}. Create one at {} (see docs/guides/cli.md)",
								cfgResult.status().message(), *cm.ConfigPath()));
			}
			skillCfg = cfgResult->skills;
			mcpServers = cfgResult->mcpServers;
			auto resolved = ResolveProviderWithConfig(std::move(*cfgResult), config.http, modelOverride);
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

		auto skillRoots = ResolveSkillRoots(config.host, skillCfg);
		runtime->skillRegistry.LoadRoots(skillRoots, config.host);
		spdlog::info("core: loaded {} skills from {} roots", runtime->skillRegistry.Size(), skillRoots.size());
		runtime->skillManager = std::make_unique<skills::SkillManager>(&runtime->skillRegistry);

		runtime->mcpManager = std::make_unique<mcp::McpConnectionManager>(config.host, std::move(mcpServers));
		runtime->toolManager = std::make_unique<tools::ToolManager>();
		RegisterBuiltInTools(*runtime->toolManager,
							 runtime->skillRegistry.Empty() ? nullptr : runtime->skillManager.get());
		(void)runtime->mcpManager->RegisterTools(*runtime->toolManager);
		return runtime;
	}

	absl::Status CoreApi::ConfigureAgent(CoreSessionRuntime& runtime, config::PermissionMode permissionMode)
	{
		if (!runtime.session)
		{
			return absl::FailedPreconditionError("session is not open");
		}

		auto* agent = runtime.session->MainAgent();
		if (agent == nullptr)
		{
			return absl::InternalError("session has no main agent");
		}

		runtime.skillManager->SetSessionId(runtime.session->Id());
		agent->SetSkillManager(runtime.skillManager.get());
		agent->SetPermissionMode(permissionMode);
		if (config.approvalCallback)
		{
			agent->SetApprovalCallback(config.approvalCallback);
		}

		agent->SetEventDispatcher([this, sessionId = runtime.session->Id()](const agent::AgentEvent& ev) {
			if (config.eventSink)
			{
				config.eventSink(CoreEvent{.sessionId = sessionId, .agentId = "main", .event = ev});
			}
		});
		return absl::OkStatus();
	}

	absl::StatusOr<std::string> CoreApi::CreateSession(CreateSessionOptions options)
	{
		auto storeStatus = EnsureSessionStore();
		if (!storeStatus.ok())
		{
			return storeStatus;
		}
		auto workdir = ResolveWorkdir(options.workdir);
		if (!workdir.ok())
		{
			return workdir.status();
		}

		auto runtime = BuildRuntime(*workdir, options.model);
		if (!runtime.ok())
		{
			return runtime.status();
		}

		session::SessionConfig sessionCfg{
			.host = config.host,
			.provider = (*runtime)->provider,
			.toolManager = (*runtime)->toolManager.get(),
			.workdir = *workdir,
			.title = options.title,
		};
		auto sess = session::Session::Create(sessionStore.get(), std::move(sessionCfg));
		if (!sess.ok())
		{
			return sess.status();
		}
		(*runtime)->session = std::move(*sess);
		auto configure = ConfigureAgent(**runtime, options.permissionMode);
		if (!configure.ok())
		{
			return configure;
		}

		auto id = (*runtime)->session->Id();
		sessions[id] = std::move(*runtime);
		return id;
	}

	absl::StatusOr<std::string> CoreApi::ResumeSession(std::string_view sessionId, CreateSessionOptions options)
	{
		auto existing = sessions.find(std::string(sessionId));
		if (existing != sessions.end() && !existing->second->closed)
		{
			return existing->first;
		}
		if (existing != sessions.end())
		{
			sessions.erase(existing);
		}

		auto storeStatus = EnsureSessionStore();
		if (!storeStatus.ok())
		{
			return storeStatus;
		}

		auto dir = sessionStore->Find(sessionId);
		if (!dir.ok())
		{
			return dir.status();
		}
		auto meta = sessionStore->ReadMeta(dir->path);
		if (!meta.ok())
		{
			return meta.status();
		}
		std::string workdir = options.workdir.empty() ? meta->workdir : options.workdir;

		auto runtime = BuildRuntime(workdir, options.model);
		if (!runtime.ok())
		{
			return runtime.status();
		}

		session::SessionConfig sessionCfg{
			.host = config.host,
			.provider = (*runtime)->provider,
			.toolManager = (*runtime)->toolManager.get(),
			.workdir = workdir,
			.title = options.title,
		};
		auto sess = session::Session::Resume(sessionStore.get(), std::move(sessionCfg), dir->sessionId);
		if (!sess.ok())
		{
			return sess.status();
		}
		(*runtime)->session = std::move(*sess);
		auto configure = ConfigureAgent(**runtime, options.permissionMode);
		if (!configure.ok())
		{
			return configure;
		}

		auto id = (*runtime)->session->Id();
		sessions[id] = std::move(*runtime);
		return id;
	}

	absl::Status CoreApi::CloseSession(std::string_view sessionId)
	{
		auto it = sessions.find(std::string(sessionId));
		if (it == sessions.end())
		{
			return absl::NotFoundError(fmt::format("session not active: {}", sessionId));
		}
		if (it->second->closed)
		{
			return absl::OkStatus();
		}

		it->second->closed = true;
		if (it->second->session)
		{
			auto status = it->second->session->Close();
			if (!status.ok())
			{
				return status;
			}
		}
		if (it->second->mcpManager)
		{
			(void)it->second->mcpManager->Shutdown();
		}
		return absl::OkStatus();
	}

	absl::StatusOr<std::vector<session::SessionInfo>> CoreApi::ListSessions(std::string_view workdir)
	{
		auto storeStatus = EnsureSessionStore();
		if (!storeStatus.ok())
		{
			return storeStatus;
		}
		auto resolvedWorkdir = ResolveWorkdir(workdir);
		if (!resolvedWorkdir.ok())
		{
			return resolvedWorkdir.status();
		}
		return sessionStore->List(*resolvedWorkdir);
	}

	absl::StatusOr<CoreApi::CoreSessionRuntime*> CoreApi::FindOpenRuntime(std::string_view sessionId)
	{
		auto it = sessions.find(std::string(sessionId));
		if (it == sessions.end())
		{
			return absl::NotFoundError(fmt::format("session not active: {}", sessionId));
		}
		if (it->second->closed)
		{
			return absl::FailedPreconditionError(fmt::format("session is closed: {}", sessionId));
		}
		return it->second.get();
	}

	absl::StatusOr<agent::PromptResult> CoreApi::Prompt(std::string_view sessionId, std::string_view text)
	{
		auto runtime = FindOpenRuntime(sessionId);
		if (!runtime.ok())
		{
			return runtime.status();
		}
		auto* agent = (*runtime)->session->MainAgent();
		if (agent == nullptr)
		{
			return absl::InternalError("session has no main agent");
		}
		spdlog::info("core: prompting model '{}' in '{}'", (*runtime)->modelName, (*runtime)->workdir);
		return agent->Prompt(text);
	}

	absl::Status CoreApi::Cancel(std::string_view sessionId)
	{
		auto runtime = FindOpenRuntime(sessionId);
		if (!runtime.ok())
		{
			return runtime.status();
		}
		(*runtime)->session->MainAgent()->Cancel();
		return absl::OkStatus();
	}

	absl::Status CoreApi::ClearContext(std::string_view sessionId)
	{
		auto runtime = FindOpenRuntime(sessionId);
		if (!runtime.ok())
		{
			return runtime.status();
		}
		(*runtime)->session->MainAgent()->ClearContext();
		return absl::OkStatus();
	}

	absl::Status CoreApi::ActivateSkill(std::string_view sessionId, std::string_view name, std::string_view args)
	{
		auto runtime = FindOpenRuntime(sessionId);
		if (!runtime.ok())
		{
			return runtime.status();
		}
		return (*runtime)->session->MainAgent()->ActivateSkill(name, args);
	}

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

} // namespace codeharness::rpc
