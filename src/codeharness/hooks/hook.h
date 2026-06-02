#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
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

} // namespace codeharness
