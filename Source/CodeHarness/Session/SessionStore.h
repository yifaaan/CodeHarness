#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Host/Host.h"
#include "Session/SessionTypes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::session
{

	// SessionStore owns the on-disk session directory layout:
	//   <root>/<workdir-key>/<sessionId>/state.json
	//   <root>/<workdir-key>/<sessionId>/agents/<agentId>/wire.jsonl
	//   <root>/session_index.jsonl   (append-only cache of {sessionId,workdir})
	//
	// All filesystem I/O goes through the injected Host* — no direct disk — so
	// the store is fully unit-testable with LocalHost against a tmp dir.
	//
	// wire.jsonl is owned by Records (the Agent constructs FilePersistence at
	// the computed path); SessionStore only owns the directory skeleton, the
	// per-session state.json, and the global index.
	class SessionStore
	{
	public:
		SessionStore(host::Host* host, std::string sessionsRoot);

		// Resolve the sessions root the way ConfigManager resolves config path:
		//   $CODEHARNESS_HOME/sessions/   if CODEHARNESS_HOME is set & non-empty
		//   $HOME/.codeharness/sessions/  otherwise (via Host::GetHome)
		static absl::StatusOr<std::string> ResolveSessionsRoot(host::Host* host);

		// "sess_" + unix-ms + "_" + 6 hex random chars. Not a UUID, but unique
		// enough for single-user local sessions; collisions are astronomically
		// unlikely given the millisecond timestamp prefix.
		static std::string NewSessionId();

		// Create a new session dir for `workdir`. Makes the directory skeleton,
		// writes the initial state.json, and appends to the session index.
		// `workdir` should be an absolute path; it is encoded into the parent
		// directory name via EncodeWorkdirKey.
		absl::StatusOr<SessionDir> Create(std::string_view workdir, std::string_view title);

		// Look up an existing session dir. Returns NotFound if absent.
		absl::StatusOr<SessionDir> Get(std::string_view workdir, std::string_view sessionId) const;

		// List sessions for a given workdir, reading each state.json for
		// metadata. Returns an empty vector (not an error) if none exist.
		absl::StatusOr<std::vector<SessionInfo>> List(std::string_view workdir) const;

		// Find a session by id prefix, scanning across all workdir keys under
		// the root. Returns NotFound if no single match. Useful for `/resume abc`.
		absl::StatusOr<SessionDir> Find(std::string_view sessionIdPrefix) const;

		// Recursively remove a session dir. NotFound if absent.
		absl::Status Remove(std::string_view workdir, std::string_view sessionId);

		// Update a session's title in state.json. NotFound if absent.
		absl::Status RenameTitle(std::string_view workdir, std::string_view sessionId, std::string_view title);

		// ---- state.json helpers (also used by Session for updatedAt writes) ----
		absl::StatusOr<SessionMeta> ReadMeta(std::string_view sessionPath) const;
		// Atomic: write state.json.tmp then Host::Rename onto state.json.
		absl::Status WriteMeta(std::string_view sessionPath, const SessionMeta& meta);

		// Append one line to session_index.jsonl.
		absl::Status AppendIndex(const SessionInfo& info);

		host::Host* HostPtr() const
		{
			return host_;
		}
		const std::string& Root() const
		{
			return root_;
		}

	private:
		// <root>/<workdir-key>
		std::string WorkdirRoot(std::string_view workdir) const;
		// <root>/<workdir-key>/<sessionId>
		std::string SessionPath(std::string_view workdir, std::string_view sessionId) const;

		host::Host* host_;
		std::string root_;
	};

} // namespace codeharness::session
