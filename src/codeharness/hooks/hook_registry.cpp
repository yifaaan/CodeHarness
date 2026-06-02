#include "codeharness/hooks/hook_registry.h"

#include <algorithm>
#include <iterator>

namespace codeharness
{

auto HookRegistry::add(HookDefinition hook) -> void
{
    hooks_[hook.event].push_back(
        RegisteredHook{
            .hook = std::move(hook),
            .order = next_order_++,
        });
}

auto HookRegistry::get(HookEvent event) const -> std::vector<HookDefinition>
{
    const auto found = hooks_.find(event);
    if (found == hooks_.end())
    {
        return {};
    }

    auto registered = found->second;
    std::ranges::stable_sort(registered, [](const RegisteredHook& lhs, const RegisteredHook& rhs) {
        if (lhs.hook.priority != rhs.hook.priority)
        {
            return lhs.hook.priority > rhs.hook.priority;
        }
        return lhs.order < rhs.order;
    });

    std::vector<HookDefinition> hooks;
    hooks.reserve(registered.size());
    std::ranges::transform(
        registered, std::back_inserter(hooks), [](RegisteredHook& entry) { return std::move(entry.hook); });
    return hooks;
}

auto HookRegistry::empty() const noexcept -> bool
{
    return hooks_.empty();
}

} // namespace codeharness
