#include "codeharness/provider/echo_provider.h"

#include "codeharness/core/error.h"

#include <optional>

namespace codeharness
{

auto EchoProvider::generate(std::span<const Message> messages) -> Result<Message>
{
    std::optional<std::string> latest_user_text;

    for (int index = messages.size() - 1; index >= 0; index--)
    {
        const auto& message = messages[index];

        if (message.role != Role::User)
        {
            continue;
        }

        const auto text = collect_text(message);
        if (!text.empty())
        {
            latest_user_text = text;
            break;
        }
    }

    if (!latest_user_text)
    {
        return fail<Message>(ErrorKind::InvalidArgument, "prompt is empty");
    }

    return make_text_message(Role::Assistant, *latest_user_text);
}

} // namespace codeharness