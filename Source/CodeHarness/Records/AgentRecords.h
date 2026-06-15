#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <functional>
#include <memory>
#include <vector>

#include "Records/RecordTypes.h"

namespace codeharness::records
{

	class RecordPersistence;

	// AgentRecords is the entry point for event sourcing.
	//
	// Lifetime: one instance per agent. Owns its RecordPersistence.
	// Threading: not thread-safe; callers must serialize Log/Replay/Flush.
	//
	// Replay contract: while IsRestoring() is true, Log() is a no-op.
	// This prevents the replay loop from re-recording the events it just read.
	class AgentRecords
	{
	public:
		// Takes ownership of `persistence` (must not be null).
		explicit AgentRecords(std::unique_ptr<RecordPersistence> persistence);
		~AgentRecords();

		AgentRecords(const AgentRecords&) = delete;
		AgentRecords& operator=(const AgentRecords&) = delete;
		AgentRecords(AgentRecords&&) = delete;
		AgentRecords& operator=(AgentRecords&&) = delete;

		// Append one record with a meta timestamp captured from system_clock.
		// Returns OkStatus when restoring_ is set (no-op short-circuit).
		absl::Status Log(AgentRecord record);

		// Read every record from the persistence backend.
		absl::StatusOr<std::vector<WireRecord>> ReadAll();

		// Replay: load all records, set restoring while invoking `apply` for
		// each. `apply` must not fail silently — return non-Ok to abort replay.
		// restoring is reset to false on exit (even on error).
		absl::Status Replay(const std::function<absl::Status(const AgentRecord&)>& apply);

		// True between the start and end of a Replay pass.
		bool IsRestoring() const noexcept;

		absl::Status Flush();
		void Close();

	private:
		std::unique_ptr<RecordPersistence> persistence;
		bool restoring = false;
	};

} // namespace codeharness::records
