#include "Cli/RunPrompt.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "Agent/Agent.h"
#include "Agent/AgentTypes.h"
#include "Config/Config.h"
#include "Config/ConfigManager.h"
#include "Config/ConfigTypes.h"
#include "Config/ProviderManager.h"
#include "Engine/LoopTypes.h"
#include "Host/Host.h"
#include "Host/LocalHost.h"
#include "Llm/BeastHttpClient.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Permission/PermissionTypes.h"
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

		// Register the full built-in tool set. Permission gating decides which
		// actually run; we register all so the model can choose.
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

		// Resolve skill roots: project-level and user-level directories.
		std::vector<skills::SkillRoot> ResolveSkillRoots(host::Host* host)
		{
			std::vector<skills::SkillRoot> roots;

			auto cwd = host->GetCwd();
			if (cwd.ok())
			{
				roots.push_back({*cwd + "/.agents/skills", skills::SkillSource::Project});
			}

			auto home = host->GetHome();
			if (home.ok())
			{
				roots.push_back({*home + "/.agents/skills", skills::SkillSource::User});
			}

			return roots;
		}

		// Parse skill option: "name" or "name:args"
		std::pair<std::string, std::string> ParseSkillOption(const std::string& skill)
		{
			auto pos = skill.find(':');
			if (pos == std::string::npos)
			{
				return {skill, ""};
			}
			return {skill.substr(0, pos), skill.substr(pos + 1)};
		}

		// Synchronous stdin y/n approval. The only "UI" for v1 non-interactive
		// mode: print the tool call, read a single character. Returns Deny on
		// EOF or anything other than an explicit 'y'.
		permission::ApprovalCallback MakeStdinApprovalCallback()
		{
			return [](std::string_view toolName, const nlohmann::json& args, std::string_view description) {
				fmt::print(stderr, "\n[permission] tool '{}' requested: {}\n  args: {}\nAllow? [y/N] ", toolName, description, args.dump());
				std::cout.flush();
				std::string line;
				if (!std::getline(std::cin, line))
				{
					fmt::print(stderr, "(EOF — denying)\n");
					return permission::PermissionDecision::Deny;
				}
				return (!line.empty() && (line[0] == 'y' || line[0] == 'Y'))
						   ? permission::PermissionDecision::Allow
						   : permission::PermissionDecision::Deny;
			};
		}

		// Wire the agent's event dispatcher so assistant text streams to stdout
		// and errors surface on stderr.
		void WireStreaming(agent::Agent& agent)
		{
			agent.SetEventDispatcher([](const agent::AgentEvent& ev) {
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
				// TurnStarted/TurnEnded/StatusChanged are silent in print mode.
			});
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
		auto cfg = *cfgResult;
		if (cfg.providers.empty())
		{
			auto path = cm.ConfigPath();
			return absl::FailedPreconditionError(fmt::format(
				"no [providers] configured in {}. Add a provider and a [models] entry before running.",
				path.ok() ? *path : std::string("<config>")));
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
		// The provider is owned by ResolvedRuntimeProvider; transfer it out.
		return std::make_pair(std::move(resolved->provider), std::move(resolved->modelName));
	}

	absl::Status Run(const CliOptions& opts, RunDeps deps)
	{
		if (deps.host == nullptr)
		{
			return absl::InvalidArgumentError("RunDeps.host must be set");
		}

		// Resolve the working directory: explicit flag, else the host's cwd.
		std::string workdir = opts.workdir;
		if (workdir.empty())
		{
			auto cwd = deps.host->GetCwd();
			if (!cwd.ok())
			{
				return cwd.status();
			}
			workdir = *cwd;
		}
		auto chdirStatus = deps.host->Chdir(workdir);
		if (!chdirStatus.ok())
		{
			return chdirStatus;
		}

		// Resolve the provider: injected (test) or from config (production).
		llm::ChatProvider* provider = nullptr;
		std::string modelName;
		std::unique_ptr<llm::ChatProvider> ownedProvider;
		if (deps.resolveProvider)
		{
			auto r = deps.resolveProvider();
			if (!r.ok())
				return r.status();
			provider = r->first;
			modelName = r->second;
		}
		else
		{
			auto resolved = ResolveProviderFromConfig(deps.host, deps.http, opts.model);
			if (!resolved.ok())
				return resolved.status();
			ownedProvider = std::move(resolved->first);
			provider = ownedProvider.get();
			modelName = std::move(resolved->second);
		}
		if (provider == nullptr)
		{
			return absl::InternalError("provider resolution returned null");
		}

		// Build the skill registry and manager.
		skills::SkillRegistry skillRegistry;
		auto skillRoots = ResolveSkillRoots(deps.host);
		skillRegistry.LoadRoots(skillRoots, deps.host);
		spdlog::info("cli: loaded {} skills from {} roots", skillRegistry.Size(), skillRoots.size());

		skills::SkillManager skillManager(&skillRegistry);

		// Build the tool set (skill tool only if skills are available).
		tools::ToolManager tm;
		RegisterBuiltInTools(tm, skillRegistry.Empty() ? nullptr : &skillManager);

		// Create the session (wires Agent + Records at the computed wire path).
		auto root = session::SessionStore::ResolveSessionsRoot(deps.host);
		if (!root.ok())
		{
			return root.status();
		}
		session::SessionStore store(deps.host, *root);

		auto sess = session::Session::Create(&store, {
														  .host = deps.host,
														  .provider = provider,
														  .toolManager = &tm,
														  .workdir = workdir,
														  .title = "cli",
													  });
		if (!sess.ok())
		{
			return sess.status();
		}

		auto* agent = (*sess)->MainAgent();
		if (agent == nullptr)
		{
			return absl::InternalError("session created but has no main agent");
		}

		WireStreaming(*agent);

		// Wire the skill manager into the agent.
		agent->SetSkillManager(&skillManager);
		skillManager.SetSessionId((*sess)->Id());

		// Permission mode.
		if (opts.yolo)
		{
			agent->SetPermissionMode(config::PermissionMode::Yolo);
		}
		else
		{
			agent->SetPermissionMode(config::PermissionMode::Manual);
			agent->SetApprovalCallback(MakeStdinApprovalCallback());
		}

		// Activate skill if --skill option is provided.
		if (!opts.skill.empty())
		{
			auto [name, args] = ParseSkillOption(opts.skill);
			spdlog::info("cli: activating skill '{}' with args '{}'", name, args);
			auto skillStatus = agent->ActivateSkill(name, args);
			if (!skillStatus.ok())
			{
				spdlog::warn("cli: skill activation failed: {}", skillStatus.message());
			}
		}

		spdlog::info("cli: prompting model '{}' in '{}'", modelName, workdir);
		auto result = agent->Prompt(opts.prompt);
		if (!result.ok())
		{
			// The error is also surfaced via the ErrorEvent dispatcher; return
			// the status so main() can set a non-zero exit code.
			return result.status();
		}

		fmt::print("\n");
		std::cout.flush();

		auto closeStatus = (*sess)->Close();
		if (!closeStatus.ok())
		{
			spdlog::warn("cli: session close failed: {}", closeStatus.message());
		}
		return absl::OkStatus();
	}

} // namespace codeharness::cli
