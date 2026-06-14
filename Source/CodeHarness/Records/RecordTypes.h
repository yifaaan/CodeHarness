#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "Engine/LoopTypes.h"
#include "Llm/Types.h"

namespace codeharness::agent
{
	enum class PromptOrigin : int;
}

namespace codeharness::records
{

	enum class RecordKind
	{
		TurnPrompt,
		TurnCancel,
		ContextAppendMessage,
		ContextAppendLoopEvent,
	};

	struct RecordMeta
	{
		std::int64_t ts = 0;
		std::string protocol = "1.0";
	};

	struct TurnPromptRecord
	{
		std::string turnId;
		std::vector<llm::ContentPart> input;
		int origin = 0; // agent::PromptOrigin as int (0 = User, 1 = SystemTrigger)
	};

	struct TurnCancelRecord
	{
		std::string turnId;
	};

	struct ContextAppendMessageRecord
	{
		llm::Message message;
	};

	struct ContextAppendLoopEventRecord
	{
		engine::LoopEvent event;
	};

	using AgentRecord = std::variant<TurnPromptRecord, TurnCancelRecord, ContextAppendMessageRecord, ContextAppendLoopEventRecord>;

	struct WireRecord
	{
		RecordMeta meta;
		AgentRecord record;
	};

} // namespace codeharness::records
