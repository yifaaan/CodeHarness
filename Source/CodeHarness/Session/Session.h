#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Session/SessionTypes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::host
{
	class Host;
}
namespace codeharness::llm
{
	class ChatProvider;
}
namespace codeharness::tools
{
	class ToolManager;
}
namespace codeharness::agent
{
	class Agent;
}
namespace codeharness::records
{
	class AgentRecords;
}

namespace codeharness::session
{

	class SessionStore;

	// Dependencies needed to construct the main Agent inside a Session.
	// All pointers are non-owning and must outlive the Session. `workdir` is
	// only consulted on Create (Resume reads it back from state.json).
	struct SessionConfig
	{
		host::Host* host = nullptr;
		llm::ChatProvider* provider = nullptr;
		tools::ToolManager* toolManager = nullptr;
		std::string workdir; // absolute cwd the session is bound to (Create only)
		std::string title;	  // human-readable; written to state.json (Create only)
	};

	// Session ties together a directory on disk (via SessionStore), a main
	// Agent, and the Agent's Records sink wired to the computed wire.jsonl
	// path. It owns the Agent and the AgentRecords; the SessionStore is
	// shared/non-owning (multiple Sessions can live under one store).
	//
	// MVP scope: a single 'main' agent per session. Subagents, RPC, fork, and
	// export are deferred.
	class Session
	{
	public:
		// Create a brand-new session: allocate id + dir via the store, then
		// construct the Agent + Records wired to <dir>/agents/main/wire.jsonl.
		static absl::StatusOr<std::unique_ptr<Session>> Create(SessionStore* store, SessionConfig cfg);

		// Resume an existing session: locate the dir (sessionId may be a unique
		// prefix), construct Agent + Records at the computed wire path, and
		// replay the wire stream into the Agent's in-memory history. `cfg.host`
		// and `cfg.provider` are required to reconstruct the Agent; `workdir`
		// and `title` are ignored (read from state.json).
		static absl::StatusOr<std::unique_ptr<Session>> Resume(SessionStore* store, SessionConfig cfg, std::string_view sessionId);

		~Session();

		Session(const Session&) = delete;
		Session& operator=(const Session&) = delete;

		// Non-owning access to the main Agent. Valid until Close(); nullptr after.
		agent::Agent* MainAgent() const { return agent_.get(); }

		const std::string& Id() const { return sessionId_; }
		const SessionMeta& Meta() const { return meta_; }
		bool IsClosed() const { return closed_; }

		// Flush records and write the updated state.json (updatedAt=now).
		// Idempotent.
		absl::Status Close();

	private:
		Session(SessionStore* store, std::string sessionId, std::string sessionPath, SessionMeta meta);

		// Build Agent + AgentRecords at the main agent's wire path and wire them
		// together. `replay` controls whether Agent::Resume() is invoked.
		absl::Status WireMainAgent(SessionConfig cfg, bool replay);

		SessionStore* store_;
		std::string sessionId_;
		std::string sessionPath_;
		SessionMeta meta_;

		std::unique_ptr<records::AgentRecords> records_;
		std::unique_ptr<agent::Agent> agent_;
		bool closed_ = false;
	};

} // namespace codeharness::session
