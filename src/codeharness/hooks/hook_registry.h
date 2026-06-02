#pragma once

#include "codeharness/hooks/hook.h"

#include <unordered_map>
#include <vector>

namespace codeharness
{

class HookRegistry
{
public:
    auto add(HookDefinition hook) -> void;
    auto get(HookEvent event) const -> std::vector<HookDefinition>;
    auto empty() const noexcept -> bool;

private:
    struct RegisteredHook
    {
        HookDefinition hook;
        std::size_t order = 0;
    };

    std::unordered_map<HookEvent, std::vector<RegisteredHook>> hooks_;
    std::size_t next_order_ = 0;
};

} // namespace codeharness
