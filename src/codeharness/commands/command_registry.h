#pragma once

#include <string>
#include <string_view>

namespace codeharness::engine {
    class QueryEngine;
}

namespace codeharness::tools {
    class ToolRegistry;
}

namespace codeharness::commands {
    struct CommandContext {
        engine::QueryEngine* engine{};
        const tools::ToolRegistry* tools{};
    };

    struct CommandResult {
        bool handled{false};       // true 表示这是本地命令，不要再发给模型
        bool should_exit{false};   // /exit 使用
        bool clear_screen{false};  // /clear 后续接 TUI/REPL 时可用
        std::string message;
    };

    class CommandRegistry {
    public:
        [[nodiscard]] auto try_dispatch(const CommandContext& context, std::string_view line) const
            -> CommandResult;

        [[nodiscard]] auto help_text() const -> std::string;
    };
}  // namespace codeharness::commands