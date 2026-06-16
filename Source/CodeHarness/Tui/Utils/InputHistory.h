#pragma once

#include <string>
#include <vector>

namespace codeharness::tui
{

/// Stores input history, persisted to disk.
class InputHistory
{
public:
	explicit InputHistory(size_t maxEntries = 100);

	void Add(const std::string& entry);
	const std::vector<std::string>& Entries() const { return entries_; }
	void Clear() { entries_.clear(); }

private:
	std::vector<std::string> entries_;
	size_t maxEntries_;
};

} // namespace codeharness::tui