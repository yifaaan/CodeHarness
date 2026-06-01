#include "codeharness/permissions/permission.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <span>
#include <utility>

namespace codeharness
{

namespace
{

auto contains_name(std::span<const std::string> values, std::string_view name) -> bool
{
    return std::ranges::any_of(values, [name](const auto& value) { return value == name; });
}

auto to_lower_ascii(std::string text) -> std::string
{
    for (char& ch : text)
    {
        ch = static_cast<char>(std::tolower(ch));
    }

    return text;
}

auto contains_text(std::string_view text, std::string_view pattern) -> bool
{
    return text.find(pattern) != std::string_view::npos;
}

//   - TODO: canonical path + glob rule
auto is_sensitive_path(const std::filesystem::path& path) -> bool
{
    auto text = to_lower_ascii(path.generic_string());

    return contains_text(text, ".ssh/") || contains_text(text, "/.ssh") || contains_text(text, ".aws/credentials") ||
           contains_text(text, ".aws/config") || contains_text(text, ".gnupg/") || contains_text(text, "/.gnupg") ||
           contains_text(text, ".docker/config.json") || contains_text(text, ".kube/config") ||
           contains_text(text, ".azure/") || contains_text(text, "/.azure") || contains_text(text, ".config/gcloud/") ||
           contains_text(text, ".codeharness/credentials.json") ||
           contains_text(text, ".openharness/credentials.json") ||
           contains_text(text, ".openharness/copilot_auth.json");
}

auto looks_dangerous_command(const std::string& command) -> bool
{
    auto text = to_lower_ascii(command);

    return contains_text(text, "rm -rf /") || contains_text(text, "del /s /q c:\\") ||
           contains_text(text, "format c:") || contains_text(text, "drop database");
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
    // 敏感路径优先
    // 不能绕过这层
    if (target_path && is_sensitive_path(*target_path))
    {
        spdlog::debug("permission deny: tool={} reason=sensitive_path path={}", tool_name, target_path->string());
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "sensitive path is blocked: " + target_path->string(),
        };
    }

    // 危险命令拒绝
    if (command && looks_dangerous_command(*command))
    {
        spdlog::debug("permission deny: tool={} reason=dangerous_command", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "dangerous command is blocked",
        };
    }

    // denied_tools 优先级高于 allowed_tools。
    if (contains_name(settings_.denied_tools, tool_name))
    {
        spdlog::debug("permission deny: tool={} reason=denied_tool", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "tool is denied by settings: " + std::string(tool_name),
        };
    }

    // 显式允许的工具可以执行。
    if (contains_name(settings_.allowed_tools, tool_name))
    {
        spdlog::debug("permission allow: tool={} reason=allowed_tool", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "tool is allowed by settings",
        };
    }

    // full_auto 允许普通工具执行。
    if (settings_.mode == PermissionMode::FullAuto)
    {
        spdlog::debug("permission allow: tool={} reason=full_auto", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "full_auto mode allows this tool",
        };
    }

    // 只读工具在 Default / Plan 下都允许。
    if (is_read_only)
    {
        spdlog::debug("permission allow: tool={} reason=read_only", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Allow,
            .reason = "read-only tool is allowed",
        };
    }

    // Plan 模式阻止修改类工具。
    if (settings_.mode == PermissionMode::Plan)
    {
        spdlog::debug("permission deny: tool={} reason=plan_mode", tool_name);
        return PermissionDecision{
            .action = PermissionAction::Deny,
            .reason = "plan mode blocks mutating tools",
        };
    }

    // Default 模式下，修改类工具需要确认。
    spdlog::debug("permission ask: tool={} reason=default_mode", tool_name);
    return PermissionDecision{
        .action = PermissionAction::Ask,
        .reason = "default mode requires confirmation for mutating tools",
    };
}

} // namespace codeharness