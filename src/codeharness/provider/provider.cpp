#include "codeharness/provider/provider.h"

#include <nonstd/expected.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include "codeharness/core/message.h"
#include "codeharness/core/result.h"

namespace
{

template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

namespace codeharness
{

auto Provider::generate(std::span<const Message> messages) -> Result<Message>
{
    Message message;
    message.role = Role::Assistant;

    std::unordered_map<std::string, int> tool_block_by_id;

    auto streamed = stream(messages, [&](const ProviderEvent& event) {
        std::visit(
            Overloaded{
                [&](const AssistantTextDelta& delta) { message.content.emplace_back(TextBlock{delta.text}); },
                [&](const ToolUseStarted& tool_use_started) {
                    /*
                        ToolUseStarted{
                            .id = "tool-use-1",
                            .name = "read_file"
                        }
                    */
                    tool_block_by_id[tool_use_started.id] = message.content.size();
                    message.content.emplace_back(
                        ToolUseBlock{.id = tool_use_started.id, .name = tool_use_started.name, .input_json = ""});
                },
                [&](const ToolUseInputDelta& delta) {
                    auto found = tool_block_by_id.find(delta.id);
                    if (found == tool_block_by_id.end())
                    {
                        return;
                    }

                    auto tool_use = std::get_if<ToolUseBlock>(&message.content[found->second]);
                    if (tool_use == nullptr)
                    {
                        return;
                    }

                    tool_use->input_json += delta.input_json_delta;
                },
                [&](const ToolUseFinished&) {},
                [&](const MessageFinished&) {}},
            event);
    });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }

    return message;

}

} // namespace codeharness