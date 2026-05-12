#include "codeharness/permissions/checker.h"

#include <absl/strings/string_view.h>

#include <nlohmann/json.hpp>

#include "absl/types/span.h"
#include "models.h"

namespace {
    [[nodiscard]] bool contains_name(absl::Span<const std::string> values, absl::string_view name) {
        return std::ranges::any_of(values, [name](const auto& value) { return value == name; });
    }

    [[nodiscard]] bool simple_match(absl::string_view value, absl::string_view pattern) {
        if (pattern == "*") {
            return true;
        }

        const auto wildcard = pattern.find('*');
        if (wildcard == std::string_view::npos) {
            return value == pattern;
        }

        const auto prefix = pattern.substr(0, wildcard);
        const auto suffix = pattern.substr(wildcard + 1);

        return value.starts_with(prefix) && value.ends_with(suffix);
    }

    [[nodiscard]] std::string json_string_field(const nlohmann::json& input,
                                                absl::string_view field_name) {
        const auto iter = input.find(std::string{field_name});
        if (iter == input.end() || !iter->is_string()) {
            return {};
        }

        return iter->get<std::string>();
    }
}  // namespace
namespace codeharness::permissions {
    PerssionChecker::PerssionChecker(PermissionSettings settings)
        : settings_{std::move(settings)} {}

    auto PerssionChecker::evaluate(absl::string_view tool_name, bool is_read_only,
                                   const nlohmann::json& input) const -> PermissionDecision {
        if (is_denied_tool(tool_name)) {
            return PermissionDecision{
                .allowed = false,
                .reason = std::string{tool_name} + " is explicitly denied",
            };
        }

        if (is_allowed_tool(tool_name)) {
            return PermissionDecision{
                .allowed = true,
                .reason = std::string{tool_name} + " is explicitly allowed",
            };
        }

        const auto path_decision = evaluate_path_rules(input);
        if (!path_decision.allowed && !path_decision.reason.empty()) {
            return path_decision;
        }

        const auto command_decision = evaluate_command_rules(input);
        if (!command_decision.allowed && !command_decision.reason.empty()) {
            return command_decision;
        }

        if (settings_.mode == PermissionMode::full_auto) {
            return PermissionDecision{
                .allowed = true,
                .reason = "full_auto mode allows all tools",
            };
        }

        if (is_read_only) {
            return PermissionDecision{
                .allowed = true,
                .reason = "read-only tools are allowed",
            };
        }

        if (settings_.mode == PermissionMode::plan) {
            return PermissionDecision{
                .allowed = false,
                .reason = "plan mode blocks mutating tools",
            };
        }

        return PermissionDecision{
            .allowed = false,
            .requires_confirmation = true,
            .reason = "mutating tools require confirmation in default mode",
        };
    }

    auto PerssionChecker::is_allowed_tool(absl::string_view tool_name) const -> bool {
        return contains_name(settings_.allowed_tools, tool_name);
    }

    auto PerssionChecker::is_denied_tool(absl::string_view tool_name) const -> bool {
        return contains_name(settings_.denied_tools, tool_name);
    }

    auto PerssionChecker::evaluate_path_rules(const nlohmann::json& input) const
        -> PermissionDecision {
        auto path = json_string_field(input, "path");
        if (path.empty()) {
            path = json_string_field(input, "file_path");
        }

        if (path.empty()) {
            return PermissionDecision{.allowed = true};
        }

        for (const auto& rule : settings_.path_rules) {
            if (simple_match(path, rule.pattern) && !rule.allow) {
                return PermissionDecision{
                    .allowed = false,
                    .reason = "path matches deny rule: " + rule.pattern,
                };
            }
        }

        return PermissionDecision{.allowed = true};
    }

    auto PerssionChecker::evaluate_command_rules(const nlohmann::json& input) const
        -> PermissionDecision {
        const auto command = json_string_field(input, "command");

        if (command.empty()) {
            return PermissionDecision{.allowed = true};
        }

        for (const auto& pattern : settings_.denied_commands) {
            if (simple_match(command, pattern)) {
                return PermissionDecision{
                    .allowed = false,
                    .reason = "command matches deny pattern: " + pattern,
                };
            }
        }

        return PermissionDecision{.allowed = true};
    }

}  // namespace codeharness::permissions