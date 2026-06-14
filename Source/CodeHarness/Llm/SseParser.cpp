#include "SseParser.h"

namespace codeharness::llm
{

	void SseParser::Feed(std::string_view data)
	{
		buffer.append(data);
	}

	std::optional<std::string> SseParser::NextEvent()
	{
		if (done)
			return std::nullopt;

		std::string line;
		while (TryExtractLine(line))
		{
			if (line.empty())
			{
				if (currentEventHasData)
				{
					if (currentEventData == "[DONE]")
					{
						done = true;
						currentEventData.clear();
						currentEventHasData = false;
						return std::nullopt;
					}
					std::string result = std::move(currentEventData);
					currentEventData.clear();
					currentEventHasData = false;
					return result;
				}
				continue;
			}

			if (line[0] == ':')
				continue;

			if (line.starts_with("data:"))
			{
				std::string_view value(line.data() + 5, line.size() - 5);
				if (!value.empty() && value.front() == ' ')
					value = value.substr(1);

				if (currentEventHasData)
					currentEventData += '\n';
				currentEventData.append(value);
				currentEventHasData = true;
			}
		}

		return std::nullopt;
	}

	bool SseParser::Done() const
	{
		return done;
	}

	void SseParser::Reset()
	{
		buffer.clear();
		currentEventData.clear();
		currentEventHasData = false;
		done = false;
	}

	bool SseParser::TryExtractLine(std::string& line)
	{
		if (buffer.empty())
			return false;

		size_t endPos = std::string::npos;
		size_t termLen = 0;

		for (size_t i = 0; i < buffer.size(); ++i)
		{
			if (buffer[i] == '\n')
			{
				endPos = i;
				termLen = 1;
				break;
			}
			if (buffer[i] == '\r')
			{
				endPos = i;
				termLen = (i + 1 < buffer.size() && buffer[i + 1] == '\n') ? 2 : 1;
				break;
			}
		}

		if (endPos == std::string::npos)
			return false;

		line.assign(buffer, 0, endPos);
		buffer.erase(0, endPos + termLen);
		return true;
	}

} // namespace codeharness::llm
