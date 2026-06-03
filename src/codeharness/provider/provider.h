#pragma once

#include "codeharness/core/message.h"
#include "codeharness/core/result.h"

#include <functional>
#include <span>
#include <string>
#include <variant>

namespace codeharness
{

struct AssistantTextDelta
{
    std::string text;
};

struct ToolUseStarted
{
    std::string id;
    std::string name;
};

struct ToolUseInputDelta
{
    std::string id;
    std::string input_json_delta;
};

struct ToolUseFinished
{
    std::string id;
};

struct MessageFinished
{
};

using ProviderEvent =
    std::variant<AssistantTextDelta, ToolUseStarted, ToolUseInputDelta, ToolUseFinished, MessageFinished>;

using ProviderEventSink = std::function<void(const ProviderEvent&)>;

class Provider
{
public:
    virtual ~Provider() = default;

    virtual auto stream(std::span<const Message> messages, const ProviderEventSink& sink) -> Result<void> = 0;

    virtual auto generate(std::span<const Message> messages) -> Result<Message>;
};

} // namespace codeharness
