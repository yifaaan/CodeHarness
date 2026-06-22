#include "Services/DesktopCoreService.h"

#include <utility>

#include "Config/ConfigTypes.h"
#include "Permission/PermissionTypes.h"
#include "absl/status/status.h"

namespace codeharness::desktop_app
{

	namespace
	{

		std::string StatusMessage(const absl::Status& status)
		{
			return std::string(status.message());
		}

	} // namespace

	DesktopCoreService::DesktopCoreService()
		: api(rpc::CoreApiConfig{
			  .host = &host,
			  .http = &http,
		  })
	{
		auto cwd = host.GetCwd();
		if (cwd.ok())
		{
			(void)host.Chdir(*cwd);
		}

		api.SetEventSink([this](const rpc::CoreEvent& event) {
			EventCallback callback;
			{
				std::lock_guard lock(mutex);
				callback = eventCallback;
			}
			if (callback)
			{
				callback(desktop::CoreEventToDesktopEvent(event));
			}
		});

		api.SetApprovalCallback([this](std::string_view toolName, const nlohmann::json& args, std::string_view description) {
			ApprovalCallback callback;
			{
				std::lock_guard lock(mutex);
				callback = approvalCallback;
			}
			if (!callback)
			{
				return permission::PermissionDecision::Deny;
			}
			desktop::DesktopPermissionRequest request{
				.toolName = std::string(toolName),
				.args = args,
				.description = std::string(description),
			};
			return callback(request) ? permission::PermissionDecision::Allow : permission::PermissionDecision::Deny;
		});

		api.SetQuestionCallback([this](const tools::QuestionRequest& request) {
			QuestionCallback callback;
			{
				std::lock_guard lock(mutex);
				callback = questionCallback;
			}
			return callback ? callback(request) : std::string{};
		});
	}

	DesktopCoreService::~DesktopCoreService()
	{
		CloseActiveSession();
		if (promptTask.valid())
		{
			promptTask.wait();
		}
	}

	void DesktopCoreService::SetEventCallback(EventCallback callback)
	{
		std::lock_guard lock(mutex);
		eventCallback = std::move(callback);
	}

	void DesktopCoreService::SetApprovalCallback(ApprovalCallback callback)
	{
		std::lock_guard lock(mutex);
		approvalCallback = std::move(callback);
	}

	void DesktopCoreService::SetQuestionCallback(QuestionCallback callback)
	{
		std::lock_guard lock(mutex);
		questionCallback = std::move(callback);
	}

	std::vector<desktop::DesktopSessionItem> DesktopCoreService::ListSessions()
	{
		auto sessions = api.ListSessions();
		if (!sessions.ok())
		{
			return {};
		}
		std::vector<desktop::DesktopSessionItem> out;
		out.reserve(sessions->size());
		for (const auto& session : *sessions)
		{
			out.push_back(desktop::SessionInfoToDesktopItem(session));
		}
		return out;
	}

	std::string DesktopCoreService::CreateSession(std::string title)
	{
		auto created = api.CreateSession(MakeSessionOptions(std::move(title)));
		if (!created.ok())
		{
			return {};
		}
		std::lock_guard lock(mutex);
		activeSessionId = *created;
		return activeSessionId;
	}

	std::string DesktopCoreService::ResumeSession(std::string sessionId)
	{
		auto resumed = api.ResumeSession(sessionId, MakeSessionOptions("desktop"));
		if (!resumed.ok())
		{
			return {};
		}
		std::lock_guard lock(mutex);
		activeSessionId = *resumed;
		return activeSessionId;
	}

	bool DesktopCoreService::HasActiveSession() const
	{
		std::lock_guard lock(mutex);
		return !activeSessionId.empty();
	}

	void DesktopCoreService::Prompt(std::string text, ErrorCallback onError)
	{
		if (promptTask.valid())
		{
			promptTask.wait();
		}

		std::string sessionId;
		{
			std::lock_guard lock(mutex);
			sessionId = activeSessionId;
		}
		if (sessionId.empty())
		{
			if (onError)
			{
				onError("no active session");
			}
			return;
		}

		promptTask = std::async(std::launch::async, [this, sessionId = std::move(sessionId), text = std::move(text), onError = std::move(onError)]() mutable {
			auto result = api.Prompt(sessionId, text);
			if (!result.ok() && onError)
			{
				onError(StatusMessage(result.status()));
			}
		});
	}

	void DesktopCoreService::Cancel()
	{
		std::string sessionId;
		{
			std::lock_guard lock(mutex);
			sessionId = activeSessionId;
		}
		if (!sessionId.empty())
		{
			(void)api.Cancel(sessionId);
		}
	}

	void DesktopCoreService::CloseActiveSession()
	{
		std::string sessionId;
		{
			std::lock_guard lock(mutex);
			sessionId = std::move(activeSessionId);
			activeSessionId.clear();
		}
		if (!sessionId.empty())
		{
			(void)api.CloseSession(sessionId);
		}
	}

	config::PermissionMode DesktopCoreService::PermissionMode() const
	{
		return config::PermissionMode::Manual;
	}

	std::vector<std::string> DesktopCoreService::ListModels()
	{
		auto models = api.ListModels();
		if (!models.ok())
		{
			return {};
		}
		std::vector<std::string> out;
		out.reserve(models->size());
		for (const auto& model : *models)
		{
			// Prefer the alias (what SetModel consumes); fall back to the raw model id.
			out.push_back(model.alias.empty() ? model.model : model.alias);
		}
		return out;
	}

	bool DesktopCoreService::SetModel(std::string model)
	{
		std::string sessionId;
		{
			std::lock_guard lock(mutex);
			sessionId = activeSessionId;
		}
		if (sessionId.empty())
		{
			return false;
		}
		return api.SetModel(sessionId, model).ok();
	}

	bool DesktopCoreService::SetThinking(bool enabled)
	{
		std::string sessionId;
		{
			std::lock_guard lock(mutex);
			sessionId = activeSessionId;
		}
		if (sessionId.empty())
		{
			return false;
		}
		// Map the boolean toggle to a medium effort level (on) vs disabled (off).
		auto effort = enabled ? std::make_optional(llm::ThinkingEffort::Medium) : std::nullopt;
		return api.SetThinking(sessionId, effort).ok();
	}

	std::string DesktopCoreService::CurrentWorkdir() const
	{
		auto cwd = host.GetCwd();
		return cwd.ok() ? *cwd : std::string{};
	}

	std::string DesktopCoreService::CurrentBranch()
	{
		// Run `git rev-parse --abbrev-ref HEAD` in the current workdir. Returns an
		// empty string if this isn't a git repo or git isn't available.
		auto proc = host.ExecWithEnv(
			{"git", "rev-parse", "--abbrev-ref", "HEAD"},
			/*cwd=*/"",
			/*env=*/{});
		if (!proc.ok() || !*proc)
		{
			return {};
		}
		auto drain = (*proc)->Drain(/*timeoutMs=*/3000, {});
		if (!drain.ok() || !drain->finished || drain->exitCode != 0)
		{
			return {};
		}
		auto& out = drain->out;
		// Trim trailing whitespace/newlines.
		auto end = out.find_last_not_of(" \t\r\n");
		return (end == std::string::npos) ? std::string{} : out.substr(0, end + 1);
	}

	rpc::CreateSessionOptions DesktopCoreService::MakeSessionOptions(std::string title) const
	{
		auto cwd = host.GetCwd();
		return {
			.workdir = cwd.ok() ? *cwd : std::string{},
			.title = std::move(title),
			.permissionMode = PermissionMode(),
		};
	}

} // namespace codeharness::desktop_app
