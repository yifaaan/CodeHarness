#pragma once

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Desktop/DesktopEventJson.h"
#include "Host/LocalHost.h"
#include "Llm/BeastHttpClient.h"
#include "Llm/Types.h"
#include "Rpc/CoreApi.h"
#include "Tools/AskUser.h"

namespace codeharness::desktop_app
{

	class DesktopCoreService
	{
	public:
		using EventCallback = std::function<void(const desktop::DesktopEvent&)>;
		using ErrorCallback = std::function<void(std::string)>;
		using ApprovalCallback = std::function<bool(const desktop::DesktopPermissionRequest&)>;
		using QuestionCallback = std::function<std::string(const tools::QuestionRequest&)>;

		DesktopCoreService();
		~DesktopCoreService();

		DesktopCoreService(const DesktopCoreService&) = delete;
		DesktopCoreService& operator=(const DesktopCoreService&) = delete;

		void SetEventCallback(EventCallback callback);
		void SetApprovalCallback(ApprovalCallback callback);
		void SetQuestionCallback(QuestionCallback callback);

		std::vector<desktop::DesktopSessionItem> ListSessions();
		std::string CreateSession(std::string title);
		std::string ResumeSession(std::string sessionId);
		bool RenameSession(std::string sessionId, std::string title);
		std::string ForkSession(std::string sessionId, std::string title = {});
		bool RemoveSession(std::string sessionId);
		bool HasActiveSession() const;
		void Prompt(std::string text, ErrorCallback onError);
		void Cancel();
		void CloseActiveSession();

		// Session-scoped runtime controls (no-ops if no active session).
		std::vector<std::string> ListModels();
		bool SetModel(std::string model);
		bool SetThinking(bool enabled);

		// Read-only environment info (independent of any session).
		std::string CurrentWorkdir() const;
		std::string CurrentBranch();

	private:
		config::PermissionMode PermissionMode() const;
		rpc::CreateSessionOptions MakeSessionOptions(std::string title) const;

		mutable std::mutex mutex;
		host::LocalHost host;
		llm::BeastHttpClient http;
		rpc::CoreApi api;
		std::string activeSessionId;
		std::future<void> promptTask;
		EventCallback eventCallback;
		ApprovalCallback approvalCallback;
		QuestionCallback questionCallback;
	};

} // namespace codeharness::desktop_app
