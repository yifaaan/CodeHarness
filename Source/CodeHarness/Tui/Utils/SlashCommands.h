#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace codeharness::tui
{

/// Parses and dispatches slash commands (/help, /clear, /exit, etc.).
class SlashCommands
{
public:
	struct Command
	{
		std::string name;
		std::string description;
		std::string args;
	};

	static bool IsSlashCommand(std::string_view text);
	static Command Parse(std::string_view text);
	static std::vector<Command> All();
};

} // namespace codeharness::tui