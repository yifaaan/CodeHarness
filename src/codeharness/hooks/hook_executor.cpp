#include "codeharness/hooks/hook_executor.h"

#include <fmt/format.h>

#include <exception>

namespace codeharness
{

namespace
{

auto matcher_applies(const HookDefinition& hook, const nlohmann::json& payload) -> bool
{
    if (!hook.matcher)
    {
        return true;
    }

    if (auto tool_name = payload.find("tool_name"); tool_name != payload.end() && tool_name->is_string())
    {
        return tool_name->get<std::string>() == *hook.matcher;
    }

    return false;
}

auto unsupported_hook_result(HookType type) -> HookResult
{
    return HookResult{
        .success = false,
        .blocked = false,
        .reason = fmt::format("hook type {} is not implemented yet", static_cast<int>(type)),
    };
}

auto execute_one(const HookDefinition& hook, const nlohmann::json& payload) -> HookResult
{
    if (!matcher_applies(hook, payload))
    {
        return HookResult{.success = true};
    }

    if (hook.type != HookType::Callback)
    {
        return unsupported_hook_result(hook.type);
    }

    if (!hook.callback)
    {
        return HookResult{.success = false, .reason = "callback hook has no callback"};
    }

    try
    {
        return hook.callback(payload);
    }
    catch (const std::exception& error)
    {
        return HookResult{.success = false, .reason = error.what()};
    }
}

} // namespace

HookExecutor::HookExecutor(const HookRegistry& registry) : registry_(registry)
{
}

auto HookExecutor::execute(HookEvent event, const nlohmann::json& payload) const -> HookExecutionResult
{
    HookExecutionResult execution;

    for (const auto& hook : registry_.get(event))
    {
        auto result = execute_one(hook, payload);

        const auto blocks = result.blocked || (!result.success && hook.block_on_failure);
        if (blocks)
        {
            execution.blocked = true;
            execution.reason = !result.reason.empty() ? result.reason : "hook blocked execution";
        }

        execution.results.push_back(std::move(result));

        if (execution.blocked)
        {
            break;
        }
    }

    return execution;
}

} // namespace codeharness
