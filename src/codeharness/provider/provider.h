#pragma once

#include "codeharness/core/message.h"
#include "codeharness/core/result.h"

#include <span>

namespace codeharness
{

class Provider
{
public:
    virtual ~Provider() = default;

    virtual auto generate(std::span<const Message> messages) -> Result<Message> = 0;
};

} // namespace codeharness