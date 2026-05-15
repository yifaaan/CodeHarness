#include "codeharness/commands/command_registry.h"

#include <fmt/format.h>

#include <array>
#include <iterator>
#include <string_view>

#include "codeharness/engine/query_engine.h"
#include "codeharness/tools/tool_registry.h"

namespace {

    struct CommandInfo {
        std::string_view name;
        std::string_view description;
    };

    constexpr auto built_in_commands = std::array{
        CommandInfo{"/help", "Show available commands"},
        CommandInfo{"/clear", "Clear conversation history"},
        CommandInfo{"/status", "Show session status"},
        CommandInfo{"/exit", "Exit the session"},
    };

}  // namespace

namespace codeharness::commands {
    auto CommandRegistry::help_text() const -> std::string {
        auto out = std::string{"Available commands:"};

        for (const auto& command : built_in_commands) {
            fmt::format_to(std::back_inserter(out), "\n{:<8} {}", command.name,
                           command.description);
        }

        return out;
    }

    auto CommandRegistry::try_dispatch(const CommandContext& context, std::string_view line) const
        -> CommandResult {
        if (line.empty() || line.front() != '/') {
            return {};
        }

        const auto command_end = line.find(' ');
        const auto command = line.substr(0, command_end);

        if (command == "/help") {
            return CommandResult{
                .handled = true,
                .message = help_text(),
            };
        }

        if (command == "/exit") {
            return CommandResult{
                .handled = true,
                .should_exit = true,
                .message = "Goodbye.",
            };
        }

        if (command == "/clear") {
            if (context.engine != nullptr) {
                context.engine->clear();
            }

            return CommandResult{
                .handled = true,
                .clear_screen = true,
                .message = "Conversation cleared.",
            };
        }

        if (command == "/status") {
            if (context.engine == nullptr) {
                return CommandResult{
                    .handled = true,
                    .message = "Engine is not initialized.",
                };
            }

            const auto messages = context.engine->messages();
            const auto usage = context.engine->total_usage();

            auto message = fmt::format("Messages: {}\nUsage: input={} output={}", messages.size(),
                                       usage.input_tokens, usage.output_tokens);

            if (context.tools != nullptr) {
                fmt::format_to(std::back_inserter(message), "\nTools: {}",
                               context.tools->list_tools().size());
            }

            return CommandResult{
                .handled = true,
                .message = std::move(message),
            };
        }

        return CommandResult{
            .handled = true,
            .message = fmt::format("Unknown command: {}\nUse /help to list commands.", command),
        };
    }
}  // namespace codeharness::commands