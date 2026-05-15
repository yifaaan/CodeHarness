#pragma once

#include <absl/strings/string_view.h>

#include <nlohmann/json.hpp>

#include "codeharness/permissions/models.h"

namespace codeharness::permissions {
    struct PathRule {
        std::string pattern;
        bool allow{true};
    };

    struct PermissionSettings {
        PermissionMode mode{PermissionMode::default_mode};
        std::vector<std::string> allowed_tools;
        std::vector<std::string> denied_tools;
        std::vector<std::string> denied_commands;
        std::vector<PathRule> path_rules;
    };

    // 表示一次工具调用是否允许
    struct PermissionDecision {
        bool allowed{};
        bool requires_confirmation{};
        std::string reason;
    };

    // 调用工具前，用来执行权限检查
    class PermissionChecker {
    public:
        explicit PermissionChecker(PermissionSettings settings);

        [[nodiscard]] auto evaluate(absl::string_view tool_name, bool is_read_only,
                                    const nlohmann::json& input) const -> PermissionDecision;

    private:
        [[nodiscard]] auto evaluate_path_rules(const nlohmann::json& input) const
            -> PermissionDecision;
        [[nodiscard]] auto evaluate_command_rules(const nlohmann::json& input) const
            -> PermissionDecision;

        PermissionSettings settings_;
    };
}  // namespace codeharness::permissions