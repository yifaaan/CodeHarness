#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <nlohmann/json.hpp>
#include <string>

#include "Records/RecordTypes.h"

namespace codeharness::records
{

	// Serialize a WireRecord to a single JSON line (no trailing newline).
	nlohmann::json WireRecordToJson(const WireRecord& wire);

	// Parse a single JSON line into a WireRecord. Returns InvalidArgumentError
	// on unknown `type` discriminator or malformed payload.
	absl::StatusOr<WireRecord> ParseWireRecord(const std::string& line);

	// ---- Inline payload serializers (reusable by Agent integration tests) ----

	nlohmann::json ContentPartToJson(const llm::ContentPart& part);
	absl::StatusOr<llm::ContentPart> ContentPartFromJson(const nlohmann::json& j);

	nlohmann::json MessageToJson(const llm::Message& msg);
	absl::StatusOr<llm::Message> MessageFromJson(const nlohmann::json& j);

	nlohmann::json LoopEventToJson(const engine::LoopEvent& event);
	absl::StatusOr<engine::LoopEvent> LoopEventFromJson(const nlohmann::json& j);

} // namespace codeharness::records
