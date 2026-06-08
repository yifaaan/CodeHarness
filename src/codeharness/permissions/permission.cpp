#include "codeharness/permissions/permission.h"

#include <re2/re2.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <span>
#include <sstream>
#include <utility>

namespace codeharness
{

namespace
{

auto contains_name(std::span<const std::string> values, std::string_view name) -> bool
{
    return std::ranges::any_of(values, [name](const auto& value) { return value == name; });
}

auto action_name(PermissionAction action) -> std::string_view
{
    switch (action)
    {
    case PermissionAction::Allow: return "allow";
    case PermissionAction::Ask: return "ask";
    case PermissionAction::Deny: return "deny";
    }

    return "unknown";
}

auto to_lower_ascii(std::string text) -> std::string
{
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    return text;
}

auto normalize_path_text(const std::filesystem::path& path) -> std::string
{
    auto text = path.generic_string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

auto glob_to_regex(std::string_view pattern) -> std::string
{
    std::ostringstream out;
    out << '^';

    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        const char ch = pattern[i];
        if (ch == '*')
        {
            if (i + 1 < pattern.size() && pattern[i + 1] == '*')
            {
                out << ".*";
                ++i;
            }
            else
            {
                out << "[^/]*";
            }
            continue;
        }

        if (ch == '?')
        {
            out << "[^/]";
            continue;
        }

        if (ch == '\\')
        {
            out << '/';
            continue;
        }

        if (std::string_view{R"(.+^$(){}[]|\)"}.find(ch) != std::string_view::npos)
        {
            out << '\\';
        }
        out << ch;
    }

    out << '$';
    return out.str();
}

auto path_glob_matches(std::string_view pattern, const std::filesystem::path& path) -> bool
{
    if (pattern.empty())
    {
        return false;
    }

    const RE2 regex{glob_to_regex(pattern)};
    if (!regex.ok())
    {
        spdlog::warn("invalid permission path glob '{}': {}", pattern, regex.error());
        return false;
    }

    return RE2::FullMatch(normalize_path_text(path), regex);
}

auto path_rule_applies(const PermissionPathRule& rule,
                       std::string_view tool_name,
                       const std::filesystem::path& path) -> bool
{
    if (!rule.tools.empty() && !contains_name(rule.tools, tool_name))
    {
        return false;
    }

    return path_glob_matches(rule.pattern, path);
}

auto command_regex_matches(std::string_view pattern, const std::string& command) -> bool
{
    if (pattern.empty())
    {
        return false;
    }

    const RE2 regex{pattern};
    if (!regex.ok())
    {
        spdlog::warn("invalid permission command regex '{}': {}", pattern, regex.error());
        return false;
    }

    return RE2::PartialMatch(command, regex);
}

auto is_sensitive_path(const std::filesystem::path& path) -> bool
{
    const auto text = to_lower_ascii(normalize_path_text(path));

    static constexpr auto sensitive_patterns = std::to_array<std::string_view>({
        ".ssh/", "/.ssh", ".aws/credentials", ".aws/config",
        ".gnupg/", "/.gnupg", ".docker/config.json", ".kube/config",
        ".azure/", "/.azure", ".config/gcloud/",
        ".codeharness/credentials.json", ".openharness/credentials.json",
        ".openharness/copilot_auth.json",
    });
    return std::ranges::any_of(sensitive_patterns,
        [&](auto p) { return text.find(p) != std::string::npos; });
}

auto looks_dangerous_command(const std::string& command) -> bool
{
    const auto text = to_lower_ascii(command);

    static constexpr auto dangerous_patterns = std::to_array<std::string_view>({
        "rm -rf /", "del /s /q c:\\", "format c:", "drop database",
    });
    return std::ranges::any_of(dangerous_patterns,
        [&](auto p) { return text.find(p) != std::string::npos; });
}

auto matching_explicit_rule_action(const PermissionSettings& settings,
                                   std::string_view tool_name,
                                   const std::optional<std::filesystem::path>& target_path,
                                   const std::optional<std::string>& command,
                                   PermissionAction action) -> std::optional<PermissionDecision>
{
    if (target_path)
    {
        for (const auto& rule : settings.path_rules)
        {
            if (rule.action == action && path_rule_applies(rule, tool_name, *target_path))
            {
                return PermissionDecision{
                    .action = action,
                    .reason = std::string{"path rule "} + std::string{action_name(action)} + " matched: " +
                              rule.pattern,
                };
            }
        }
    }

    if (command)
    {
        for (const auto& rule : settings.command_rules)
        {
            if (rule.action == action && command_regex_matches(rule.pattern, *command))
            {
                return PermissionDecision{
                    .action = action,
                    .reason = std::string{"command rule "} + std::string{action_name(action)} + " matched: " +
                              rule.pattern,
                };
            }
        }
    }

    return std::nullopt;
}

auto session_grant_matches(const PermissionSessionGrant& grant,
                           std::string_view tool_name,
                           const std::optional<std::filesystem::path>& target_path,
                           const std::optional<std::string>& command) -> bool
{
    if (grant.tool_name != tool_name)
    {
        return false;
    }

    if (grant.path)
    {
        return target_path && *grant.path == normalize_path_text(*target_path);
    }
    if (target_path)
    {
        return false;
    }

    if (grant.command)
    {
        return command && *grant.command == *command;
    }
    return !command;
}

auto matching_session_grant(const PermissionSettings& settings,
                            std::string_view tool_name,
                            const std::optional<std::filesystem::path>& target_path,
                            const std::optional<std::string>& command) -> bool
{
    return std::ranges::any_of(settings.session_grants, [&](const auto& grant) {
        return session_grant_matches(grant, tool_name, target_path, command);
    });
}

} // namespace

PermissionChecker::PermissionChecker(PermissionSettings settings) : settings_(std::move(settings))
{
}

auto PermissionChecker::evaluate(
    std::string_view tool_name,
    bool is_read_only,
    std::optional<std::filesystem::path> target_path,
    std::optional<std::string> command) const -> PermissionDecision
{
    if (target_path && is_sensitive_path(*target_path))
    {
        spdlog::debug("permission deny: tool={} reason=sensitive_path path={}", tool_name, target_path->string());
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "sensitive path is blocked: " + target_path->string(),
        };
    }

    if (command && looks_dangerous_command(*command))
    {
        spdlog::debug("permission deny: tool={} reason=dangerous_command", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "dangerous command is blocked",
        };
    }

    if (contains_name(settings_.denied_tools, tool_name))
    {
        spdlog::debug("permission deny: tool={} reason=denied_tool", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "tool is denied by settings: " + std::string(tool_name),
        };
    }

    if (auto decision = matching_explicit_rule_action(
            settings_, tool_name, target_path, command, PermissionAction::Deny))
    {
        spdlog::debug("permission deny: tool={} reason={}", tool_name, decision->reason);
        return *decision;
    }

    if (is_read_only)
    {
        spdlog::debug("permission allow: tool={} reason=read_only", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "read-only tool is allowed",
        };
    }

    if (settings_.mode == PermissionMode::Plan)
    {
        spdlog::debug("permission deny: tool={} reason=plan_mode", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "plan mode blocks mutating tools",
        };
    }

    if (auto decision = matching_explicit_rule_action(
            settings_, tool_name, target_path, command, PermissionAction::Allow))
    {
        spdlog::debug("permission allow: tool={} reason={}", tool_name, decision->reason);
        return *decision;
    }

    if (contains_name(settings_.allowed_tools, tool_name))
    {
        spdlog::debug("permission allow: tool={} reason=allowed_tool", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "tool is allowed by settings",
        };
    }

    if (matching_session_grant(settings_, tool_name, target_path, command))
    {
        spdlog::debug("permission allow: tool={} reason=session_grant", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "tool is allowed by this session",
        };
    }

    if (settings_.mode == PermissionMode::FullAuto)
    {
        spdlog::debug("permission allow: tool={} reason=full_auto", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "full_auto mode allows this tool",
        };
    }

    spdlog::debug("permission ask: tool={} reason=default_mode", tool_name);
    return PermissionDecision{
        .action = PermissionAction::Ask,
        .reason = "default mode requires confirmation for mutating tools",
    };
}

} // namespace codeharness
