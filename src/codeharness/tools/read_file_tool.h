#pragma once

#include <absl/strings/string_view.h>

#include "codeharness/tools/base.h"

namespace codeharness::tools {
    class ReadFileTool final : public Tool {
        [[nodiscard]] virtual auto name() const -> absl::string_view override;
        [[nodiscard]] virtual auto description() const -> absl::string_view override;
        [[nodiscard]] virtual auto input_schema() const -> nlohmann::json override;
        // 判断这次调用是否只读
        [[nodiscard]] virtual auto is_read_only(const nlohmann::json& input) const -> bool override;

        virtual auto execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
            -> ToolResult override;
    };
}  // namespace codeharness::tools