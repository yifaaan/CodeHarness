#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <vector>

#include "Records/RecordTypes.h"

namespace codeharness::records
{

	// Abstract persistence backend for the wire.jsonl stream.
	// Implementations must preserve insertion order on Read().
	class RecordPersistence
	{
	public:
		virtual ~RecordPersistence() = default;

		// Append one record (atomic from caller's perspective). Must not mutate.
		virtual absl::Status Append(const WireRecord& record) = 0;

		// Read all records back in insertion order. Empty result is valid for a
		// fresh (or nonexistent) stream.
		virtual absl::StatusOr<std::vector<WireRecord>> Read() = 0;

		// Ensure all previously appended records are durably persisted.
		virtual absl::Status Flush() = 0;

		// Release any open handles. Idempotent.
		virtual absl::Status Close() = 0;
	};

} // namespace codeharness::records
