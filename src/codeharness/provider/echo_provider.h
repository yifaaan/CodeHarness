#pragma once

#include "codeharness/provider/provider.h"

namespace codeharness
{

class EchoProvider final : public Provider
{
public:
    auto generate(std::span<const Message> messages) -> Result<Message> override;
};

} // namespace codeharness