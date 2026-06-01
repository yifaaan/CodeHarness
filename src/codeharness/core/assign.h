#pragma once

#include "codeharness/core/result.h"

#include <nonstd/expected.hpp>
#include <utility>

namespace codeharness
{

// 解包 Result<Value> 并赋给 target；失败则把同一个 error 透传成 Result<void>。
template <typename Target, typename Value>
auto assign(Target& target, Result<Value>&& source) -> Result<void>
{
    if (!source)
    {
        return nonstd::make_unexpected(source.error());
    }
    target = std::move(*source);
    return {};
}

} // namespace codeharness
