#include "codeharness/provider/openai_stream_parser.h"
#include "codeharness/provider/provider_json_helpers.h"

#include <nlohmann/json.hpp>

#include <string>

namespace codeharness
{

namespace
{

auto event_type(const nlohmann::json& input) -> std::string
{
    return json_string_value(input, "type");
}

auto error_message_from(const nlohmann::json& input) -> std::string
{
    if (const auto found = input.find("error"); found != input.end() && found->is_object())
    {
        auto message = json_string_value(*found, "message");
        if (!message.empty())
        {
            return message;
        }
    }

    if (const auto found = input.find("response"); found != input.end() && found->is_object())
    {
        if (const auto error = found->find("error"); error != found->end() && error->is_object())
        {
            auto message = json_string_value(*error, "message");
            if (!message.empty())
            {
                return message;
            }
        }
    }

    return "OpenAI response failed";
}

auto usage_object_from_completed_event(const nlohmann::json& event) -> const nlohmann::json*
{
    if (const auto usage = event.find("usage"); usage != event.end() && usage->is_object())
    {
        return &*usage;
    }

    if (const auto response = event.find("response"); response != event.end() && response->is_object())
    {
        if (const auto usage = response->find("usage"); usage != response->end() && usage->is_object())
        {
            return &*usage;
        }
    }

    return nullptr;
}

auto key_for_item(const nlohmann::json& event, const nlohmann::json& item = nlohmann::json::object()) -> std::string
{
    auto key = json_string_value(event, "item_id");
    if (key.empty())
    {
        key = json_string_value(item, "id");
    }
    if (key.empty())
    {
        key = json_string_value(item, "call_id");
    }
    return key;
}

auto without_trailing_cr(std::string_view line) -> std::string_view
{
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }
    return line;
}

} // namespace

auto OpenAIStreamParser::feed(std::string_view chunk) -> ParsedEvent
{
    ParsedEvent result;

    for (const auto& event : sse_.feed(chunk))
    {
        if (event.data == "[DONE]")
        {
            auto tool_events = flush_pending_tools();
            result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
            result.events.push_back(MessageFinished{});
            result.done = true;
            finished_ = true;
            continue;
        }

        try
        {
            auto json = nlohmann::json::parse(event.data);
            handle_json_event(json, result);
        }
        catch (const nlohmann::json::exception& error)
        {
            bool recovered = false;
            std::size_t start = 0;
            while (start <= event.data.size())
            {
                const auto end = event.data.find('\n', start);
                auto line = without_trailing_cr(
                    end == std::string::npos ? std::string_view{event.data}.substr(start)
                                             : std::string_view{event.data}.substr(start, end - start));
                if (!line.empty())
                {
                    if (auto json = try_parse_json(line))
                    {
                        handle_json_event(*json, result);
                        recovered = true;
                        if (result.done)
                        {
                            return result;
                        }
                    }
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }

            if (!recovered)
            {
                result.error = std::string{"OpenAI stream JSON parse error: "} + error.what();
                result.done = true;
                return result;
            }
        }
    }

    return result;
}

auto OpenAIStreamParser::handle_json_event(const nlohmann::json& event, ParsedEvent& result) -> void
{
    const auto type = event_type(event);

    if (type == "error" || type == "response.failed" || type == "response.incomplete")
    {
        result.error = error_message_from(event);
        result.done = true;
        finished_ = true;
        return;
    }

    if (type == "response.output_text.delta")
    {
        auto delta = json_string_value(event, "delta");
        if (!delta.empty())
        {
            result.events.push_back(AssistantTextDelta{std::move(delta)});
        }
        return;
    }

    if (type == "response.output_item.added" || type == "response.output_item.done")
    {
        const auto item = event.value("item", nlohmann::json::object());
        if (json_string_value(item, "type") != "function_call")
        {
            return;
        }

        auto key = key_for_item(event, item);
        if (key.empty())
        {
            key = std::to_string(pending_tool_calls_.size());
        }

        auto& tool = pending_tool_calls_[key];
        if (auto id = json_string_value(item, "call_id"); !id.empty())
        {
            tool.id = std::move(id);
        }
        if (tool.id.empty())
        {
            tool.id = key;
        }
        if (auto name = json_string_value(item, "name"); !name.empty())
        {
            tool.name = std::move(name);
        }
        if (auto arguments = json_string_value(item, "arguments"); !arguments.empty() && !tool.streamed_arguments)
        {
            tool.arguments = std::move(arguments);
        }

        emit_tool_start(key, result);
        if (type == "response.output_item.done")
        {
            emit_tool_finish(key, result);
        }
        return;
    }

    if (type == "response.function_call_arguments.delta")
    {
        auto key = key_for_item(event);
        if (key.empty())
        {
            key = std::to_string(pending_tool_calls_.size());
        }

        auto& tool = pending_tool_calls_[key];
        if (tool.id.empty())
        {
            tool.id = key;
        }
        emit_tool_start(key, result);

        auto delta = json_string_value(event, "delta");
        if (!delta.empty())
        {
            tool.streamed_arguments = true;
            emit_tool_arguments(key, std::move(delta), result);
        }
        return;
    }

    if (type == "response.function_call_arguments.done")
    {
        auto key = key_for_item(event);
        if (key.empty())
        {
            return;
        }

        auto& tool = pending_tool_calls_[key];
        if (auto arguments = json_string_value(event, "arguments"); !arguments.empty() && !tool.streamed_arguments)
        {
            tool.arguments = std::move(arguments);
        }
        emit_tool_finish(key, result);
        return;
    }

    if (type == "response.completed")
    {
        if (const auto* usage = usage_object_from_completed_event(event))
        {
            result.events.push_back(usage->get<ProviderUsage>());
        }
        auto tool_events = flush_pending_tools();
        result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
        result.events.push_back(MessageFinished{});
        result.done = true;
        finished_ = true;
    }
}

auto OpenAIStreamParser::emit_tool_start(std::string_view key, ParsedEvent& result) -> void
{
    auto found = pending_tool_calls_.find(std::string{key});
    if (found == pending_tool_calls_.end())
    {
        return;
    }

    auto& tool = found->second;
    if (tool.started)
    {
        return;
    }

    if (tool.id.empty())
    {
        tool.id = std::string{key};
    }

    tool.started = true;
    result.events.push_back(ToolUseStarted{.id = tool.id, .name = tool.name});
}

auto OpenAIStreamParser::emit_tool_arguments(std::string_view key, std::string arguments, ParsedEvent& result) -> void
{
    auto found = pending_tool_calls_.find(std::string{key});
    if (found == pending_tool_calls_.end())
    {
        return;
    }

    emit_tool_start(key, result);
    if (!arguments.empty())
    {
        result.events.push_back(ToolUseInputDelta{.id = found->second.id, .input_json_delta = std::move(arguments)});
    }
}

auto OpenAIStreamParser::emit_tool_finish(std::string_view key, ParsedEvent& result) -> void
{
    auto found = pending_tool_calls_.find(std::string{key});
    if (found == pending_tool_calls_.end())
    {
        return;
    }

    auto& tool = found->second;
    emit_tool_start(key, result);
    if (tool.finished)
    {
        return;
    }

    if (!tool.arguments.empty())
    {
        emit_tool_arguments(key, std::move(tool.arguments), result);
    }

    tool.finished = true;
    result.events.push_back(ToolUseFinished{.id = tool.id});
}

auto OpenAIStreamParser::flush_pending_tools() -> std::vector<ProviderEvent>
{
    std::vector<ProviderEvent> events;

    for (auto& [key, tool] : pending_tool_calls_)
    {
        if (tool.finished)
        {
            continue;
        }

        ParsedEvent partial;
        emit_tool_finish(key, partial);
        events.insert(events.end(), partial.events.begin(), partial.events.end());
    }

    pending_tool_calls_.clear();
    return events;
}

} // namespace codeharness
