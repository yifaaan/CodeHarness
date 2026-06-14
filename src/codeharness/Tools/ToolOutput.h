#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tools
{

	// Maximum characters in a single tool result (prevents context exhaustion).
	inline constexpr std::size_t kMaxOutputChars = 50000;
	// Maximum characters per output line; longer lines are truncated.
	inline constexpr std::size_t kMaxLineChars = 2000;

	// Truncate output to fit within `maxChars` total and `maxLineChars` per line.
	// A notice is appended when truncation occurs so the model knows output was cut.
	std::string TruncateOutput(std::string_view output, std::size_t maxChars = kMaxOutputChars, std::size_t maxLineChars = kMaxLineChars);

	// Split text into lines, handling both LF and CRLF line endings. An empty input
	// yields a single empty line (so a blank file reads as one line).
	std::vector<std::string> SplitLines(std::string_view text);

	// Render lines with 1-based (or `start`-based) line-number prefixes: "<n>\t<line>".
	std::string NumberLines(const std::vector<std::string> &lines, int start = 1);

} // namespace codeharness::tools
