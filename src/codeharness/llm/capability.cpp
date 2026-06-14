#include "capability.h"

#include <regex>
#include <string>

namespace codeharness::llm
{

ModelCapability GetCapability(std::string_view model_name)
{
	std::string name(model_name);
	std::transform(name.begin(), name.end(), name.begin(), ::tolower);

	ModelCapability cap;

	if (std::regex_search(name, std::regex(R"(^gpt-4o)")))
	{
		cap.imageIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 128000;
	}
	else if (std::regex_search(name, std::regex(R"(^gpt-4\.1)")))
	{
		cap.imageIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 1047576;
	}
	else if (std::regex_search(name, std::regex(R"(^gpt-4\.5)")))
	{
		cap.imageIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 128000;
	}
	else if (std::regex_search(name, std::regex(R"(^gpt-4-turbo)")))
	{
		cap.imageIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 128000;
	}
	else if (std::regex_search(name, std::regex(R"(^gpt-3\.5)")))
	{
		cap.toolUse = true;
		cap.maxContextTokens = 16385;
	}
	else if (std::regex_search(name, std::regex(R"(^o\d)")))
	{
		cap.thinking = true;
		cap.toolUse = true;
		cap.maxContextTokens = 200000;
	}
	else if (std::regex_search(name, std::regex(R"(claude.*4)")))
	{
		cap.imageIn = true;
		cap.thinking = true;
		cap.toolUse = true;
		cap.maxContextTokens = 200000;
	}
	else if (std::regex_search(name, std::regex(R"(claude.*3)")))
	{
		cap.imageIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 200000;
	}
	else if (std::regex_search(name, std::regex(R"(gemini-2\.5)")))
	{
		cap.imageIn = true;
		cap.videoIn = true;
		cap.audioIn = true;
		cap.thinking = true;
		cap.toolUse = true;
		cap.maxContextTokens = 1048576;
	}
	else if (std::regex_search(name, std::regex(R"(gemini-2\.0)")))
	{
		cap.imageIn = true;
		cap.videoIn = true;
		cap.audioIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 1048576;
	}
	else if (std::regex_search(name, std::regex(R"(gemini-1\.5)")))
	{
		cap.imageIn = true;
		cap.videoIn = true;
		cap.audioIn = true;
		cap.toolUse = true;
		cap.maxContextTokens = 1048576;
	}
	else
	{
		cap = UnknownCapability;
	}

	return cap;
}

} // namespace codeharness::llm
