#pragma once

#include <absl/strings/string_view.h>

#include "codeharness/tools/base.h"

namespace codeharness::tools {

    class WebFetchTool final : public Tool {
    public:
        [[nodiscard]] auto name() const -> absl::string_view override;
        [[nodiscard]] auto description() const -> absl::string_view override;
        [[nodiscard]] auto input_schema() const -> nlohmann::json override;
        [[nodiscard]] auto is_read_only(const nlohmann::json& input) const -> bool override;

        auto execute(const nlohmann::json& input, const ToolExecutionContext& ctx)
            -> absl::StatusOr<std::string> override;
    };

}  // namespace codeharness::tools
