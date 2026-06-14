#include "Tools/ToolOutput.h"

#include <fmt/format.h>

#include <string>

namespace codeharness::tools
{

	std::vector<std::string> SplitLines(std::string_view text)
	{
		std::vector<std::string> lines;
		if (text.empty())
			return lines;

		std::size_t pos = 0;
		while (pos <= text.size())
		{
			auto next = text.find('\n', pos);
			std::string_view line = (next == std::string_view::npos) ? text.substr(pos) : text.substr(pos, next - pos);
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}
			lines.emplace_back(line);
			if (next == std::string_view::npos)
				break;
			pos = next + 1;
		}

		// Treat a trailing newline as a line terminator rather than the start of a
		// new empty line: "a\nb\n" yields ["a","b"], not ["a","b",""].
		if (!lines.empty() && lines.back().empty() && text.back() == '\n')
		{
			lines.pop_back();
		}
		return lines;
	}

	std::string NumberLines(const std::vector<std::string> &lines, int start)
	{
		std::string out;
		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			out += fmt::format("{}\t{}\n", start + static_cast<int>(i), lines[i]);
		}
		return out;
	}

	std::string TruncateOutput(std::string_view output, std::size_t maxChars, std::size_t maxLineChars)
	{
		auto lines = SplitLines(output);

		for (auto &line : lines)
		{
			if (line.size() > maxLineChars)
			{
				line.resize(maxLineChars);
				line += "... [line truncated]";
			}
		}

		std::string joined;
		for (auto &line : lines)
		{
			if (joined.size() + line.size() + 1 >= maxChars)
			{
				auto remaining = maxChars - joined.size();
				if (remaining > 0)
					joined.append(line, 0, remaining);
				joined += fmt::format("\n... [output truncated at {} chars]", maxChars);
				return joined;
			}
			joined += line;
			joined += '\n';
		}
		return joined;
	}

} // namespace codeharness::tools
