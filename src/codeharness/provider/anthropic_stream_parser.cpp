#include "codeharness/provider/anthropic_stream_parser.h"

#include <nlohmann/json.hpp>

#include <string>

namespace codeharness
{

namespace
{

auto json_string(const nlohmann::json& input, std::string_view key) -> std::string
{
    const auto found = input.find(key);
    if (found == input.end() || found->is_null() || !found->is_string())
    {
        return {};
    }
    return found->get<std::string>();
}

auto json_int(const nlohmann::json& input, std::string_view key, int fallback = 0) -> int
{
    const auto found = input.find(key);
    if (found == input.end() || !found->is_number_integer())
    {
        return fallback;
    }
    return found->get<int>();
}

auto error_message_from(const nlohmann::json& input) -> std::string
{
    if (const auto error = input.find("error"); error != input.end() && error->is_object())
    {
        auto message = json_string(*error, "message");
        if (!message.empty())
        {
            return message;
        }
    }
    return "Anthropic response failed";
}

} // namespace

auto AnthropicStreamParser::feed(std::string_view chunk) -> ParsedEvent
{
    ParsedEvent result;

    for (const auto& event : sse_.feed(chunk))
    {
        try
        {
            auto json = nlohmann::json::parse(event.data);
            handle_json_event(json, result);
        }
        catch (const nlohmann::json::exception& error)
        {
            result.error = std::string{"Anthropic stream JSON parse error: "} + error.what();
            result.done = true;
            return result;
        }
    }

    return result;
}

auto AnthropicStreamParser::handle_json_event(const nlohmann::json& event, ParsedEvent& result) -> void
{
    const auto type = json_string(event, "type");

    if (type == "error")
    {
        result.error = error_message_from(event);
        result.done = true;
        return;
    }

    if (type == "content_block_start")
    {
        const auto index = json_int(event, "index");
        const auto content_block = event.value("content_block", nlohmann::json::object());
        if (json_string(content_block, "type") != "tool_use")
        {
            return;
        }

        auto& tool = tool_blocks_[index];
        tool.id = json_string(content_block, "id");
        tool.name = json_string(content_block, "name");
        if (tool.id.empty())
        {
            tool.id = std::to_string(index);
        }
        emit_tool_start(index, result);
        return;
    }

    if (type == "content_block_delta")
    {
        const auto index = json_int(event, "index");
        const auto delta = event.value("delta", nlohmann::json::object());
        const auto delta_type = json_string(delta, "type");

        if (delta_type == "text_delta")
        {
            auto text = json_string(delta, "text");
            if (!text.empty())
            {
                result.events.push_back(AssistantTextDelta{std::move(text)});
            }
            return;
        }

        if (delta_type == "input_json_delta")
        {
            auto& tool = tool_blocks_[index];
            if (tool.id.empty())
            {
                tool.id = std::to_string(index);
            }
            emit_tool_start(index, result);

            auto partial = json_string(delta, "partial_json");
            if (!partial.empty())
            {
                result.events.push_back(ToolUseInputDelta{.id = tool.id, .input_json_delta = std::move(partial)});
            }
            return;
        }
    }

    if (type == "content_block_stop")
    {
        emit_tool_finish(json_int(event, "index"), result);
        return;
    }

    if (type == "message_stop")
    {
        auto tool_events = flush_tools();
        result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
        result.events.push_back(MessageFinished{});
        result.done = true;
    }
}

auto AnthropicStreamParser::emit_tool_start(int index, ParsedEvent& result) -> void
{
    auto found = tool_blocks_.find(index);
    if (found == tool_blocks_.end())
    {
        return;
    }

    auto& tool = found->second;
    if (tool.started)
    {
        return;
    }

    tool.started = true;
    result.events.push_back(ToolUseStarted{.id = tool.id, .name = tool.name});
}

auto AnthropicStreamParser::emit_tool_finish(int index, ParsedEvent& result) -> void
{
    auto found = tool_blocks_.find(index);
    if (found == tool_blocks_.end())
    {
        return;
    }

    auto& tool = found->second;
    emit_tool_start(index, result);
    if (tool.finished)
    {
        return;
    }

    tool.finished = true;
    result.events.push_back(ToolUseFinished{.id = tool.id});
}

auto AnthropicStreamParser::flush_tools() -> std::vector<ProviderEvent>
{
    std::vector<ProviderEvent> events;

    for (auto& [index, tool] : tool_blocks_)
    {
        if (tool.finished)
        {
            continue;
        }

        ParsedEvent partial;
        emit_tool_finish(index, partial);
        events.insert(events.end(), partial.events.begin(), partial.events.end());
    }

    tool_blocks_.clear();
    return events;
}

} // namespace codeharness
