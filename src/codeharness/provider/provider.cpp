#include "codeharness/provider/provider.h"

#include <nonstd/expected.hpp>

#include <utility>

#include "codeharness/core/error.h"
#include "codeharness/core/event_collector.h"
#include "codeharness/core/result.h"

namespace codeharness
{

auto Provider::generate(std::span<const Message> messages) -> Result<Message>
{
    ProviderEventCollector collector;
    collector.message().role = Role::Assistant;

    auto streamed = stream(messages, [&](const ProviderEvent& event) { collector.on_event(event); });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }

    return collector.finalize();
}

} // namespace codeharness
