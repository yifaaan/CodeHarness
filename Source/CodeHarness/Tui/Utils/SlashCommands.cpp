#include "Tui/Utils/SlashCommands.h"

#include <vector>

namespace codeharness::tui
{

/*static*/ bool SlashCommands::IsSlashCommand(std::string_view text)
{
	return !text.empty() && text[0] == '/';
}

/*static*/ SlashCommands::Command SlashCommands::Parse(std::string_view text)
{
	Command cmd;
	if (text.empty() || text[0] != '/') return cmd;
	auto rest = text.substr(1);
	auto space = rest.find(' ');
	if (space == std::string_view::npos)
	{
		cmd.name = std::string(rest);
	}
	else
	{
		cmd.name = std::string(rest.substr(0, space));
		cmd.args = std::string(rest.substr(space + 1));
	}
	return cmd;
}

/*static*/ std::vector<SlashCommands::Command> SlashCommands::All()
{
	return {
		{"/help", "Show help", ""},
		{"/clear", "Clear context", ""},
		{"/exit", "Exit the TUI", ""},
		{"/model", "Switch model", "[name]"},
	};
}

} // namespace codeharness::tui