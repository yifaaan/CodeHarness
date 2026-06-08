#pragma once

#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness
{

enum class HookEvent
{
    SessionStart,
    SessionEnd,
    PreCompact,
    PostCompact,
    PreToolUse,
    PostToolUse,
    UserPromptSubmit,
    Notification,
    Stop,
    SubagentStop,
};

enum class HookType
{
    Callback,
    Command,
    Http,
    Prompt,
    Agent,
};

struct HookResult
{
    bool success = true;
    bool blocked = false;
    std::string output;
    std::string reason;
};

using HookCallback = std::function<HookResult(const nlohmann::json&)>;

struct HookDefinition
{
    HookEvent event = HookEvent::PreToolUse;
    HookType type = HookType::Callback;
    int priority = 0;
    // 过滤条件
    // 设置了 matcher = "write_file"：遇到 tool_name = write_file时，才触发 hook
    std::optional<std::string> matcher; 
    bool block_on_failure = false;
    int timeout_seconds = 30;
    nlohmann::json config;
    HookCallback callback;
};

struct HookExecutionResult
{
    bool blocked = false;
    std::string reason;
    std::vector<HookResult> results;
};

auto hook_event_from_string(std::string_view value) -> std::optional<HookEvent>;
auto hook_event_to_string(HookEvent event) -> std::string_view;
auto hook_type_from_string(std::string_view value) -> std::optional<HookType>;
auto hook_type_to_string(HookType type) -> std::string_view;

auto hook_definition_from_json(const nlohmann::json& json, std::string_view context) -> Result<HookDefinition>;
auto hook_definition_to_json(const HookDefinition& hook) -> nlohmann::json;

} // namespace codeharness
