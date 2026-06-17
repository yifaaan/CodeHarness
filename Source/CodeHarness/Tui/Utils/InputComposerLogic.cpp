#include "Tui/Utils/InputComposerLogic.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace codeharness::tui
{
	namespace
	{
		std::string Lower(std::string_view value)
		{
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return out;
		}

		bool StartsWithInsensitive(std::string_view value, std::string_view prefix)
		{
			if (prefix.size() > value.size())
			{
				return false;
			}
			return Lower(value.substr(0, prefix.size())) == Lower(prefix);
		}

		bool CommandMatches(const SlashCommands::Command& command, std::string_view prefix)
		{
			if (prefix.empty() || StartsWithInsensitive(command.name, prefix))
			{
				return true;
			}
			return std::any_of(command.aliases.begin(), command.aliases.end(), [prefix](const std::string& alias) {
				return StartsWithInsensitive(alias, prefix);
			});
		}

		std::string CommandDisplay(const SlashCommands::Command& command)
		{
			std::string display = "/" + command.name;
			if (!command.argumentHint.empty())
			{
				display += " " + command.argumentHint;
			}
			if (!command.aliases.empty())
			{
				display += "  (";
				for (size_t i = 0; i < command.aliases.size(); ++i)
				{
					if (i > 0)
					{
						display += ", ";
					}
					display += "/" + command.aliases[i];
				}
				display += ")";
			}
			display += "  " + command.description;
			return display;
		}

		bool IsMentionBoundary(char ch)
		{
			return std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '(' || ch == '[' || ch == '{';
		}

		std::string NormalizeSlashes(std::string value)
		{
			std::replace(value.begin(), value.end(), '\\', '/');
			return value;
		}

		std::string PrefixDir(std::string_view prefix)
		{
			auto normalized = NormalizeSlashes(std::string(prefix));
			auto slash = normalized.find_last_of('/');
			return slash == std::string::npos ? "" : normalized.substr(0, slash + 1);
		}

		std::string Leaf(std::string_view prefix)
		{
			auto normalized = NormalizeSlashes(std::string(prefix));
			auto slash = normalized.find_last_of('/');
			return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
		}

		std::filesystem::path BaseDir(std::string_view workdir, std::string_view prefix)
		{
			auto normalized = NormalizeSlashes(std::string(prefix));
			auto slash = normalized.find_last_of('/');
			if (slash == std::string::npos)
			{
				return std::filesystem::path(std::string(workdir));
			}
			return std::filesystem::path(std::string(workdir)) / normalized.substr(0, slash);
		}

		bool IsHiddenName(const std::filesystem::path& path)
		{
			auto name = path.filename().string();
			return !name.empty() && name[0] == '.';
		}
	}

	std::vector<SlashSuggestion> BuildSlashSuggestions(std::string_view input, std::size_t limit)
	{
		if (input.empty() || input[0] != '/' || input.find_first_of(" \t\r\n") != std::string_view::npos)
		{
			return {};
		}

		auto prefix = input.substr(1);
		auto commands = SlashCommands::All();
		std::stable_sort(commands.begin(), commands.end(), [](const auto& lhs, const auto& rhs) {
			return lhs.priority == rhs.priority ? lhs.name < rhs.name : lhs.priority > rhs.priority;
		});

		std::vector<SlashSuggestion> out;
		for (const auto& command : commands)
		{
			if (!CommandMatches(command, prefix))
			{
				continue;
			}
			out.push_back({.command = command, .display = CommandDisplay(command)});
			if (out.size() >= limit)
			{
				break;
			}
		}
		return out;
	}

	std::optional<FileMentionQuery> FindFileMentionQuery(std::string_view input, std::size_t cursor)
	{
		cursor = std::min(cursor, input.size());
		auto beforeCursor = input.substr(0, cursor);
		auto at = beforeCursor.find_last_of('@');
		if (at == std::string_view::npos)
		{
			return std::nullopt;
		}
		if (at > 0 && !IsMentionBoundary(input[at - 1]))
		{
			return std::nullopt;
		}
		auto prefix = beforeCursor.substr(at + 1);
		if (prefix.find_first_of(" \t\r\n") != std::string_view::npos)
		{
			return std::nullopt;
		}
		return FileMentionQuery{.at = at, .prefix = std::string(prefix)};
	}

	std::vector<FileMentionSuggestion> BuildFileMentionSuggestions(std::string_view workdir,
												 std::string_view prefix,
												 std::size_t limit)
	{
		std::error_code ec;
		auto base = BaseDir(workdir, prefix);
		if (!std::filesystem::exists(base, ec) || !std::filesystem::is_directory(base, ec))
		{
			return {};
		}

		const auto leaf = Lower(Leaf(prefix));
		const auto prefixDir = PrefixDir(prefix);
		std::vector<FileMentionSuggestion> out;
		for (const auto& entry : std::filesystem::directory_iterator(base, ec))
		{
			if (ec)
			{
				break;
			}
			if (IsHiddenName(entry.path()))
			{
				continue;
			}
			auto filename = entry.path().filename().string();
			if (!leaf.empty() && !StartsWithInsensitive(filename, leaf))
			{
				continue;
			}
			const bool isDirectory = entry.is_directory(ec);
			auto insert = prefixDir + filename + (isDirectory ? "/" : "");
			out.push_back({.insertText = insert, .display = "@" + insert, .isDirectory = isDirectory});
			if (out.size() >= limit)
			{
				break;
			}
		}
		std::sort(out.begin(), out.end(), [](const auto& lhs, const auto& rhs) {
			if (lhs.isDirectory != rhs.isDirectory)
			{
				return lhs.isDirectory && !rhs.isDirectory;
			}
			return lhs.display < rhs.display;
		});
		return out;
	}

	std::string ApplyFileMentionCompletion(std::string_view input,
									 const FileMentionQuery& query,
									 std::string_view insertText)
	{
		std::string out(input.substr(0, query.at));
		out += "@";
		out += insertText;
		out += input.substr(query.at + 1 + query.prefix.size());
		return out;
	}

	bool IsShiftEnterInputSequence(std::string_view input)
	{
		return input == "\x1b[13;2u" || input == "\x1b[13;2~" || input == "\x1b[27;2;13~";
	}

	ComposerSubmitAction SubmitAction(bool shiftHeld, bool inputEmpty)
	{
		if (shiftHeld)
		{
			return ComposerSubmitAction::InsertNewline;
		}
		return inputEmpty ? ComposerSubmitAction::None : ComposerSubmitAction::Submit;
	}

	bool CanUseHistoryForInput(std::string_view input)
	{
		return input.find('\n') == std::string_view::npos;
	}

	bool ApplyHistoryUp(const std::vector<std::string>& entries, HistoryNavigationState& nav, std::string& input)
	{
		if (!CanUseHistoryForInput(input) || nav.cursor == 0)
		{
			return false;
		}
		if (nav.cursor == entries.size())
		{
			nav.savedInput = input;
		}
		--nav.cursor;
		input = entries[nav.cursor];
		return true;
	}

	bool ApplyHistoryDown(const std::vector<std::string>& entries, HistoryNavigationState& nav, std::string& input)
	{
		if (!CanUseHistoryForInput(input) || nav.cursor >= entries.size())
		{
			return false;
		}
		if (nav.cursor + 1 < entries.size())
		{
			++nav.cursor;
			input = entries[nav.cursor];
		}
		else
		{
			++nav.cursor;
			input = nav.savedInput;
			nav.savedInput.clear();
		}
		return true;
	}

} // namespace codeharness::tui
