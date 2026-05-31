#include "codeharness/engine/engine.h"

#include <nonstd/expected.hpp>

#include <span>
#include <utility>

namespace codeharness
{

Engine::Engine(Provider& provider) : provider_(provider)
{
}

auto Engine::run(const RunRequest& request) -> Result<RunResult>
{
    RunResult result;
    result.messages.push_back(make_text_message(Role::User, request.prompt));

    auto assistant_message = provider_.generate(result.messages);
    if (!assistant_message)
    {
        return nonstd::make_unexpected(assistant_message.error());
    }

    result.output_text = collect_text(*assistant_message);
    result.messages.push_back(std::move(*assistant_message));

    return result;
}

} // namespace codeharness