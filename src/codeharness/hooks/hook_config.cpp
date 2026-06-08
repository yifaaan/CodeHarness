#include "codeharness/hooks/hook.h"

#include "codeharness/core/json_parse.h"

#include <nonstd/expected.hpp>

#include <string>
#include <utility>

namespace codeharness
{

namespace
{

auto read_optional_string(const nlohmann::json& json, std::string_view field, std::string_view context)
    -> Result<std::optional<std::string>>
{
    return read_optional_json_field<std::string>(json, field, context);
}

auto read_optional_bool(const nlohmann::json& json, std::string_view field, std::string_view context)
    -> Result<std::optional<bool>>
{
    return read_optional_json_field<bool>(json, field, context);
}

auto read_optional_int(const nlohmann::json& json, std::string_view field, std::string_view context)
    -> Result<std::optional<int>>
{
    return read_optional_json_field<int>(json, field, context);
}

auto validate_command_config(const nlohmann::json& config, std::string_view context) -> Result<void>
{
    if (!config.is_object())
    {
        return fail<void>(ErrorKind::InvalidArgument, std::string{context} + " config must be an object");
    }

    const auto has_command = config.contains("command");
    const auto has_argv = config.contains("argv");
    if (!has_command && !has_argv)
    {
        return fail<void>(
            ErrorKind::InvalidArgument,
            std::string{context} + " command hook requires config.command or config.argv");
    }

    if (has_command && !config.at("command").is_string())
    {
        return fail<void>(ErrorKind::InvalidArgument, std::string{context} + " config.command must be a string");
    }

    if (has_argv)
    {
        const auto& argv = config.at("argv");
        if (!argv.is_array() || argv.empty())
        {
            return fail<void>(
                ErrorKind::InvalidArgument,
                std::string{context} + " config.argv must be a non-empty string array");
        }

        for (const auto& item : argv)
        {
            if (!item.is_string())
            {
                return fail<void>(
                    ErrorKind::InvalidArgument,
                    std::string{context} + " config.argv must be a non-empty string array");
            }
        }
    }

    return {};
}

} // namespace

auto hook_event_from_string(std::string_view value) -> std::optional<HookEvent>
{
    if (value == "session_start" || value == "SessionStart") return HookEvent::SessionStart;
    if (value == "session_end" || value == "SessionEnd") return HookEvent::SessionEnd;
    if (value == "pre_compact" || value == "PreCompact") return HookEvent::PreCompact;
    if (value == "post_compact" || value == "PostCompact") return HookEvent::PostCompact;
    if (value == "pre_tool_use" || value == "PreToolUse") return HookEvent::PreToolUse;
    if (value == "post_tool_use" || value == "PostToolUse") return HookEvent::PostToolUse;
    if (value == "user_prompt_submit" || value == "UserPromptSubmit") return HookEvent::UserPromptSubmit;
    if (value == "notification" || value == "Notification") return HookEvent::Notification;
    if (value == "stop" || value == "Stop") return HookEvent::Stop;
    if (value == "subagent_stop" || value == "SubagentStop") return HookEvent::SubagentStop;
    return std::nullopt;
}

auto hook_event_to_string(HookEvent event) -> std::string_view
{
    switch (event)
    {
    case HookEvent::SessionStart: return "session_start";
    case HookEvent::SessionEnd: return "session_end";
    case HookEvent::PreCompact: return "pre_compact";
    case HookEvent::PostCompact: return "post_compact";
    case HookEvent::PreToolUse: return "pre_tool_use";
    case HookEvent::PostToolUse: return "post_tool_use";
    case HookEvent::UserPromptSubmit: return "user_prompt_submit";
    case HookEvent::Notification: return "notification";
    case HookEvent::Stop: return "stop";
    case HookEvent::SubagentStop: return "subagent_stop";
    }

    return "pre_tool_use";
}

auto hook_type_from_string(std::string_view value) -> std::optional<HookType>
{
    if (value == "callback" || value == "Callback") return HookType::Callback;
    if (value == "command" || value == "Command") return HookType::Command;
    if (value == "http" || value == "Http") return HookType::Http;
    if (value == "prompt" || value == "Prompt") return HookType::Prompt;
    if (value == "agent" || value == "Agent") return HookType::Agent;
    return std::nullopt;
}

auto hook_type_to_string(HookType type) -> std::string_view
{
    switch (type)
    {
    case HookType::Callback: return "callback";
    case HookType::Command: return "command";
    case HookType::Http: return "http";
    case HookType::Prompt: return "prompt";
    case HookType::Agent: return "agent";
    }

    return "command";
}

auto hook_definition_from_json(const nlohmann::json& json, std::string_view context) -> Result<HookDefinition>
{
    if (!json.is_object())
    {
        return fail<HookDefinition>(ErrorKind::InvalidArgument, std::string{context} + " must be an object");
    }

    auto event_text = read_json_field<std::string>(json, "event", context);
    if (!event_text)
    {
        return nonstd::make_unexpected(event_text.error());
    }

    auto event = hook_event_from_string(*event_text);
    if (!event)
    {
        return fail<HookDefinition>(
            ErrorKind::InvalidArgument,
            std::string{context} + " has unsupported event: " + *event_text);
    }

    auto type_text = read_json_field<std::string, JsonFieldMode::optional_with_default>(
        json,
        "type",
        context,
        std::string{"command"});
    if (!type_text)
    {
        return nonstd::make_unexpected(type_text.error());
    }

    auto type = hook_type_from_string(*type_text);
    if (!type)
    {
        return fail<HookDefinition>(
            ErrorKind::InvalidArgument,
            std::string{context} + " has unsupported type: " + *type_text);
    }
    if (*type != HookType::Command)
    {
        return fail<HookDefinition>(
            ErrorKind::InvalidArgument,
            std::string{context} + " only supports command hooks in configuration");
    }

    HookDefinition hook;
    hook.event = *event;
    hook.type = *type;

    if (auto priority = read_optional_int(json, "priority", context); !priority)
    {
        return nonstd::make_unexpected(priority.error());
    }
    else if (*priority)
    {
        hook.priority = **priority;
    }

    if (auto matcher = read_optional_string(json, "matcher", context); !matcher)
    {
        return nonstd::make_unexpected(matcher.error());
    }
    else
    {
        hook.matcher = std::move(*matcher);
    }

    if (auto block = read_optional_bool(json, "block_on_failure", context); !block)
    {
        return nonstd::make_unexpected(block.error());
    }
    else if (*block)
    {
        hook.block_on_failure = **block;
    }

    if (auto timeout = read_optional_int(json, "timeout_seconds", context); !timeout)
    {
        return nonstd::make_unexpected(timeout.error());
    }
    else if (*timeout)
    {
        if (**timeout < 1)
        {
            return fail<HookDefinition>(
                ErrorKind::InvalidArgument,
                std::string{context} + " timeout_seconds must be greater than zero");
        }
        hook.timeout_seconds = **timeout;
    }

    const auto config = json.find("config");
    if (config == json.end())
    {
        return fail<HookDefinition>(ErrorKind::InvalidArgument, std::string{context} + " requires object field: config");
    }
    if (auto validated = validate_command_config(*config, context); !validated)
    {
        return nonstd::make_unexpected(validated.error());
    }
    hook.config = *config;

    return hook;
}

auto hook_definition_to_json(const HookDefinition& hook) -> nlohmann::json
{
    nlohmann::json json{
        {"event", hook_event_to_string(hook.event)},
        {"type", hook_type_to_string(hook.type)},
        {"priority", hook.priority},
        {"block_on_failure", hook.block_on_failure},
        {"timeout_seconds", hook.timeout_seconds},
        {"config", hook.config},
    };
    if (hook.matcher)
    {
        json["matcher"] = *hook.matcher;
    }

    return json;
}

} // namespace codeharness
