#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace codeharness::llm
{

class SseParser
{
public:
	void Feed(std::string_view data);
	std::optional<std::string> NextEvent();
	bool Done() const;
	void Reset();

private:
	bool TryExtractLine(std::string& line);

	std::string buffer;
	std::string currentEventData;
	bool currentEventHasData = false;
	bool done = false;
};

} // namespace codeharness::llm
