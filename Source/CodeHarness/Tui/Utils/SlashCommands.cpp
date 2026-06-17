#include "Tui/Utils/SlashCommands.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace codeharness::tui
{
	namespace
	{
		using Command = SlashCommands::Command;

		std::string_view Trim(std::string_view text)
		{
			constexpr std::string_view whitespace = " \t\r\n";
			auto start = text.find_first_not_of(whitespace);
			if (start == std::string_view::npos)
			{
				return {};
			}
			auto end = text.find_last_not_of(whitespace);
			return text.substr(start, end - start + 1);
		}

		const std::vector<Command>& Catalog()
		{
			static const std::vector<Command> commands = {
				{"yolo", {"yes", "mode"}, "Toggle auto-approve mode", "", 100, "always"},
				{"auto", {}, "Toggle auto permission mode", "", 100, "always"},
				{"permission", {}, "Select permission mode", "", 100, "always"},
				{"settings", {"config"}, "Open TUI settings", "", 100, "always"},
				{"plan", {}, "Toggle plan mode", "[clear]", 100, "always"},
				{"swarm", {}, "Toggle swarm mode or run one task in swarm mode", "[on|off|task]", 100, "idle-only"},
				{"model", {}, "Switch LLM model", "[name]", 100, "always"},
				{"provider", {"providers"}, "Manage AI providers", "", 95, "always"},
				{"btw", {}, "Ask a forked side agent a question", "[question]", 90, "always"},
				{"help", {"h", "?"}, "Show available commands and shortcuts", "", 80, "always"},
				{"new", {"clear"}, "Start a fresh session in the current workspace", "", 80, "idle-only"},
				{"sessions", {"resume"}, "Browse and resume sessions", "", 80, "idle-only"},
				{"tasks", {"task"}, "Browse background tasks", "", 80, "always"},
				{"compact", {}, "Compact the conversation context", "", 80, "idle-only"},
				{"goal", {}, "Start or manage an autonomous goal", "[status|pause|resume|cancel|replace|next]", 80, "mixed"},
				{"fork", {}, "Fork the current session", "[title]", 80, "idle-only"},
				{"undo", {}, "Withdraw the last prompt from the transcript", "", 80, "idle-only"},
				{"mcp", {}, "Show MCP server status", "", 60, "always"},
				{"plugins", {}, "Manage plugins", "", 60, "always"},
				{"experiments", {"experimental"}, "Manage experimental features", "", 60, "idle-only"},
				{"reload", {}, "Reload session and apply config settings", "", 60, "idle-only"},
				{"reload-tui", {}, "Reload only TUI preferences", "", 60, "always"},
				{"title", {"rename"}, "Set or show session title", "[title]", 60, "always"},
				{"usage", {}, "Show session tokens and context window", "", 60, "always"},
				{"status", {}, "Show current session and runtime status", "", 60, "always"},
				{"feedback", {}, "Send feedback", "", 60, "always"},
				{"editor", {}, "Set the external editor", "", 60, "always"},
				{"theme", {}, "Set the terminal UI theme", "", 60, "always"},
				{"init", {}, "Analyze the codebase and generate AGENTS.md", "", 50, "idle-only"},
				{"logout", {"disconnect"}, "Log out of a configured provider", "", 40, "idle-only"},
				{"login", {}, "Select a platform and authenticate", "", 40, "idle-only"},
				{"export-md", {"export"}, "Export current session as Markdown", "[path]", 40, "idle-only"},
				{"export-debug-zip", {}, "Export current session as a debug ZIP archive", "[path]", 40, "idle-only"},
				{"exit", {"quit", "q"}, "Exit the application", "", 20, "always"},
				{"version", {}, "Show version information", "", 20, "always"},
			};
			return commands;
		}

		bool Matches(const Command& command, std::string_view name)
		{
			if (command.name == name)
			{
				return true;
			}
			return std::find(command.aliases.begin(), command.aliases.end(), name) != command.aliases.end();
		}
	} // namespace

	/*static*/ bool SlashCommands::IsSlashCommand(std::string_view text)
	{
		return !text.empty() && text[0] == '/';
	}

	/*static*/ std::optional<SlashCommands::ParsedCommand> SlashCommands::Parse(std::string_view text)
	{
		if (text.empty() || text[0] != '/')
			return std::nullopt;
		auto rest = Trim(text.substr(1));
		if (rest.empty())
		{
			return std::nullopt;
		}

		ParsedCommand cmd;
		auto space = rest.find(' ');
		if (space == std::string_view::npos)
		{
			cmd.name = std::string(rest);
		}
		else
		{
			cmd.name = std::string(rest.substr(0, space));
			cmd.args = std::string(Trim(rest.substr(space + 1)));
		}
		if (cmd.name.find('/') != std::string::npos)
		{
			return std::nullopt;
		}
		return cmd;
	}

	/*static*/ std::vector<SlashCommands::Command> SlashCommands::All()
	{
		return Catalog();
	}

	/*static*/ const SlashCommands::Command* SlashCommands::Find(std::string_view name)
	{
		const auto& commands = Catalog();
		auto it = std::find_if(commands.begin(), commands.end(), [name](const Command& command) {
			return Matches(command, name);
		});
		return it == commands.end() ? nullptr : &*it;
	}

	/*static*/ std::string SlashCommands::CanonicalName(std::string_view name)
	{
		if (const auto* command = Find(name))
		{
			return command->name;
		}
		return std::string(name);
	}

} // namespace codeharness::tui
