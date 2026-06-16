#include "Tui/Utils/InputHistory.h"

namespace codeharness::tui
{

InputHistory::InputHistory(size_t maxEntries)
	: maxEntries_(maxEntries)
{
}

void InputHistory::Add(const std::string& entry)
{
	if (entry.empty()) return;
	if (!entries_.empty() && entries_.back() == entry) return;
	entries_.push_back(entry);
	if (entries_.size() > maxEntries_)
	{
		entries_.erase(entries_.begin());
	}
}

} // namespace codeharness::tui