#pragma once

#include <cstdint>
#include <vector>

#include "Context/TokenEstimate.h"
#include "Llm/Types.h"

namespace codeharness::context
{

	// Owns the conversation history plus a cached running token estimate.
	// The Agent holds one of these in place of a bare std::vector<llm::Message>,
	// so it can ask "how full is the context?" in O(1) without recounting every
	// message on each turn.
	//
	// The token count is an estimate (see TokenEstimate); it is not authoritative.
	class ContextMemory
	{
	public:
		ContextMemory() = default;

		// Append a message and update the cached token count.
		void Append(llm::Message msg);

		// Replace the whole history (used after compaction). Recomputes the
		// token count from scratch.
		void ReplaceAll(std::vector<llm::Message> msgs);

		// Read-only access to the underlying messages.
		const std::vector<llm::Message>& Messages() const
		{
			return messages;
		}

		// Cached estimate (not recomputed on access).
		int64_t TokenCount() const
		{
			return tokens;
		}

		bool Empty() const
		{
			return messages.empty();
		}
		std::size_t Size() const
		{
			return messages.size();
		}

		void Clear();

	private:
		std::vector<llm::Message> messages;
		int64_t tokens = 0;
	};

} // namespace codeharness::context
