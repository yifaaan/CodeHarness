#pragma once

#include <optional>
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
			std::vector<std::string> aliases;
			std::string description;
			std::string argumentHint;
			int priority = 0;
			std::string availability = "idle-only";
		};

		struct ParsedCommand
		{
			std::string name;
			std::string args;
		};

		static bool IsSlashCommand(std::string_view text);
		static std::optional<ParsedCommand> Parse(std::string_view text);
		static std::vector<Command> All();
		static const Command* Find(std::string_view name);
		static std::string CanonicalName(std::string_view name);
	};

} // namespace codeharness::tui
