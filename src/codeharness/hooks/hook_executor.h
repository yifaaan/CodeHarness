#pragma once

#include "codeharness/hooks/hook_registry.h"

#include <nlohmann/json.hpp>

namespace codeharness
{

class HookExecutor
{
public:
    explicit HookExecutor(const HookRegistry& registry);

    // payload 是一个任意的 JSON 对象，包含了当前事件相关的信息。比如在 PreToolUse 事件中，payload 可能包含 tool_name、args 等字段。
    auto execute(HookEvent event, const nlohmann::json& payload) const -> HookExecutionResult;

private:
    const HookRegistry& registry_;
};

} // namespace codeharness
