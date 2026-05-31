#include "codeharness/engine/engine.h"

#include <nonstd/expected.hpp>

#include <span>
#include <utility>

namespace codeharness
{

Engine::Engine(Provider& provider) : provider_(provider)
{
}

Engine::Engine(Provider& provider, const ToolRegistry& tools) : provider_(provider), tools_(&tools)
{
}

auto Engine::run(const RunRequest& request) -> Result<RunResult>
{
    RunResult result;
    result.messages.push_back(make_text_message(Role::User, request.prompt));

    for (int turn = 0; turn < request.options.max_turns; turn++)
    {
        auto assistant_message = provider_.generate(result.messages);
        if (!assistant_message)
        {
            return nonstd::make_unexpected(assistant_message.error());
        }

        auto tool_uses = collect_tool_uses(*assistant_message);
        result.messages.push_back(std::move(*assistant_message));

        if (tool_uses.empty())
        {
            result.output_text = collect_text(result.messages.back());
            return result;
        }

        std::vector<ToolResultBlock> tool_results;
        tool_results.reserve(tool_uses.size());

        for (auto& tool_use : tool_uses)
        {
            tool_results.push_back(execute_tool_use(tool_use));
        }

        result.messages.push_back(make_tool_result_message(std::move(tool_results)));
    }

    return fail<RunResult>(ErrorKind::Provider, "max turns exceeded");
}

auto Engine::execute_tool_use(const ToolUseBlock& tool_use) const -> ToolResultBlock
{
    ToolRequest request{.id = tool_use.id, .name = tool_use.name, .input_json = tool_use.input_json};

    ToolContext context;
    context.cwd = std::filesystem::current_path();

    auto response = tools_->execute(request, context);
    if (!response)
    {
        return ToolResultBlock{
            .tool_use_id = std::move(tool_use.id),
            .content = std::move(response.error().message),
            .is_error = true,
        };
    }

    return ToolResultBlock{
        .tool_use_id = std::move(tool_use.id),
        .content = std::move(response->content),
        .is_error = response->is_error,
    };
}

} // namespace codeharness