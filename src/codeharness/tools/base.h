#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace codeharness::tools {
    // 工具执行结果
    struct ToolResult {
        std::string output;
        bool is_error{};
    };

    // 工具执行上下文
    struct ToolExecutionContext {
        std::filesystem::path cwd;
    };

    class Tool {
    public:
        virtual ~Tool() = default;

        [[nodiscard]] virtual auto name() const -> std::string = 0;
        [[nodiscard]] virtual auto description() const -> std::string = 0;
        [[nodiscard]] virtual auto input_schema() const -> nlohmann::json = 0;
        // 判断这次调用是否只读
        [[nodiscard]] virtual auto is_read_only(const nlohmann::json& input) const -> bool = 0;

        virtual auto execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
            -> ToolResult = 0;

        [[nodiscard]] nlohmann::json api_schema() const {
            return {
                {"name", name()},
                {"description", description()},
                {"input_schema", input_schema()},
            };
        }
    };

}  // namespace codeharness::tools