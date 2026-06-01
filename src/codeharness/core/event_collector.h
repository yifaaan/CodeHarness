#pragma once

#include "codeharness/core/error.h"
#include "codeharness/core/message.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/core/result.h"
#include "codeharness/provider/provider.h"

#include <nonstd/expected.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace codeharness
{

// 把 ProviderEvent 流累积成单个 Message。
//
// 规则：
//   - AssistantTextDelta    -> 追加一个 TextBlock
//   - ToolUseStarted        -> 在 message.content 末尾追加一个空 input_json 的 ToolUseBlock
//   - ToolUseInputDelta     -> 把 delta 拼到已注册 ToolUseBlock 的 input_json 末尾
//   - ToolUseFinished       -> 仅校验 id 已被注册
//   - MessageFinished       -> 忽略（用于驱动 stream 结束）
//
// 错误处理：保留第一个错误，后续错误不再覆盖；调用方通过 finalize() 拿到 Result。
class ProviderEventCollector
{
public:
    auto on_event(const ProviderEvent& event) -> void
    {
        std::visit(
            Overloaded{
                [this](const AssistantTextDelta& delta) { message_.content.emplace_back(TextBlock{delta.text}); },
                [this](const ToolUseStarted& started) {
                    if (tool_block_by_id_.contains(started.id))
                    {
                        set_error(ErrorKind::Provider, "duplicate tool use id: " + started.id);
                        return;
                    }
                    tool_block_by_id_[started.id] = message_.content.size();
                    message_.content.emplace_back(
                        ToolUseBlock{.id = started.id, .name = started.name, .input_json = ""});
                },
                [this](const ToolUseInputDelta& delta) {
                    auto found = tool_block_by_id_.find(delta.id);
                    if (found == tool_block_by_id_.end())
                    {
                        set_error(ErrorKind::Provider, "tool input delta before tool start: " + delta.id);
                        return;
                    }
                    auto tool_use = std::get_if<ToolUseBlock>(&message_.content[found->second]);
                    if (tool_use == nullptr)
                    {
                        set_error(ErrorKind::Internal, "tool block type mismatch");
                        return;
                    }
                    tool_use->input_json += delta.input_json_delta;
                },
                [this](const ToolUseFinished& finished) {
                    if (!tool_block_by_id_.contains(finished.id))
                    {
                        set_error(ErrorKind::Provider, "tool finished before tool start: " + finished.id);
                    }
                },
                [](const MessageFinished&) {}},
            event);
    }

    auto has_error() const noexcept -> bool
    {
        return event_error_.has_value();
    }

    auto finalize() const -> Result<Message>
    {
        if (event_error_)
        {
            return nonstd::make_unexpected(*event_error_);
        }
        return message_;
    }

    auto message() noexcept -> Message&
    {
        return message_;
    }

private:
    auto set_error(ErrorKind kind, std::string text) -> void
    {
        if (!event_error_)
        {
            event_error_ = CodeHarnessError{kind, std::move(text)};
        }
    }

    Message message_;
    std::unordered_map<std::string, std::size_t> tool_block_by_id_;
    std::optional<CodeHarnessError> event_error_;
};

} // namespace codeharness
