#pragma once

#include <absl/strings/string_view.h>

#include "codeharness/tools/base.h"

namespace codeharness::tools {
    // 写文件工具：创建或覆盖工作区内的文本文件
    class WriteFileTool final : public Tool {
        [[nodiscard]] auto name() const -> absl::string_view override;
        [[nodiscard]] auto description() const -> absl::string_view override;
        [[nodiscard]] auto input_schema() const -> nlohmann::json override;
        // write_file 不是只读操作
        [[nodiscard]] auto is_read_only(const nlohmann::json& input) const -> bool override;

        auto execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
            -> absl::StatusOr<std::string> override;
    };
}  // namespace codeharness::tools
