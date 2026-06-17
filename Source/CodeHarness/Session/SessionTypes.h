#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace codeharness::session
{

	// Metadata for a session, persisted as state.json inside the session dir.
	// This is the authoritative per-session state; wire.jsonl (under Records)
	// is the authoritative conversation history.
	struct SessionMeta
	{
		std::string title;
		std::int64_t createdAt = 0; // unix epoch ms
		std::int64_t updatedAt = 0; // unix epoch ms
		std::string workdir;		// absolute working dir the session was created in
	};

	// Lightweight view of a session dir on disk, returned by Create/Get/Find.
	struct SessionDir
	{
		std::string sessionId;
		std::string path; // <root>/<workdir-key>/<sessionId>

		// Full path to an agent's wire.jsonl inside this session dir.
		std::string AgentWirePath(std::string_view agentId) const;
	};

	// Per-session summary used by List() and the session index.
	struct SessionInfo
	{
		std::string sessionId;
		std::string title;
		std::string workdir;
		std::int64_t createdAt = 0;
		std::int64_t updatedAt = 0;
		int agentCount = 1;
	};

} // namespace codeharness::session
