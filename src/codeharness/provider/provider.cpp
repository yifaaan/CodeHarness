#include "codeharness/provider/provider.h"

#include <nonstd/expected.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "codeharness/core/error.h"
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
    std::optional<CodeHarnessError> event_error;

    auto set_event_error = [&](ErrorKind kind, std::string text) {
        if (!event_error)
        {
            event_error = CodeHarnessError{kind, std::move(text)};
        }
    };
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
                    if (tool_block_by_id.contains(tool_use_started.id))
                    {
                        set_event_error(ErrorKind::Provider, "duplicate tool use id: " + tool_use_started.id);
                        return;
                    }
                    tool_block_by_id[tool_use_started.id] = message.content.size();
                    message.content.emplace_back(
                        ToolUseBlock{.id = tool_use_started.id, .name = tool_use_started.name, .input_json = ""});
                },
                [&](const ToolUseInputDelta& delta) {
                    auto found = tool_block_by_id.find(delta.id);
                    if (found == tool_block_by_id.end())
                    {
                        set_event_error(ErrorKind::Provider, "tool input delta before tool start: " + delta.id);
                        return;
                    }

                    auto tool_use = std::get_if<ToolUseBlock>(&message.content[found->second]);
                    if (tool_use == nullptr)
                    {
                        set_event_error(ErrorKind::Internal, "tool block type mismatch");
                        return;
                    }

                    tool_use->input_json += delta.input_json_delta;
                },
                [&](const ToolUseFinished& finished) {
                    if (!tool_block_by_id.contains(finished.id))
                    {
                        set_event_error(ErrorKind::Provider, "tool finished before tool start: " + finished.id);
                    }
                },
                [&](const MessageFinished&) {}},
            event);
    });

    if (!streamed)
    {
        return nonstd::make_unexpected(streamed.error());
    }
    if (event_error)
    {
        return nonstd::make_unexpected(*event_error);
    }
    return message;
}

} // namespace codeharness