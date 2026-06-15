#include "Context/ContextMemory.h"

#include <utility>

#include "Context/TokenEstimate.h"

namespace codeharness::context
{

	void ContextMemory::Append(llm::Message msg)
	{
		tokens += EstimateTokens(msg);
		messages.push_back(std::move(msg));
	}

	void ContextMemory::ReplaceAll(std::vector<llm::Message> msgs)
	{
		messages = std::move(msgs);
		tokens = EstimateTokens(messages);
	}

	void ContextMemory::Clear()
	{
		messages.clear();
		tokens = 0;
	}

} // namespace codeharness::context
