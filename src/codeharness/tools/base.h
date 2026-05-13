#pragma once

#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>

#include <filesystem>
#include <nlohmann/json.hpp>

namespace codeharness::tools {
    // Tool execution context.
    struct ToolExecutionContext {
        std::filesystem::path cwd;
    };

    class Tool {
    public:
        virtual ~Tool() = default;

        [[nodiscard]] virtual auto name() const -> absl::string_view = 0;
        [[nodiscard]] virtual auto description() const -> absl::string_view = 0;
        [[nodiscard]] virtual auto input_schema() const -> nlohmann::json = 0;
        // Whether this tool call is read-only.
        [[nodiscard]] virtual auto is_read_only(const nlohmann::json& input) const -> bool = 0;

        virtual auto execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
            -> absl::StatusOr<std::string> = 0;

        [[nodiscard]] nlohmann::json api_schema() const {
            return {
                {"name", name()},
                {"description", description()},
                {"input_schema", input_schema()},
            };
        }
    };

}  // namespace codeharness::tools
