#pragma once

#include "codeharness/provider/provider.h"
#include "codeharness/network/sse_parser.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness
{

class OpenAIStreamParser
{
public:
    struct ParsedEvent
    {
        std::vector<ProviderEvent> events;
        bool done = false;
        std::string error;
    };

    auto feed(std::string_view chunk) -> ParsedEvent;

private:
    network::SseParser sse_;

    struct ToolAccum
    {
        std::string id;
        std::string name;
        std::string arguments;
        bool started = false;
        bool finished = false;
        bool streamed_arguments = false;
    };
    std::map<std::string, ToolAccum> pending_tool_calls_;
    bool finished_ = false;

    auto handle_json_event(const nlohmann::json& event, ParsedEvent& result) -> void;
    auto emit_tool_start(std::string_view key, ParsedEvent& result) -> void;
    auto emit_tool_arguments(std::string_view key, std::string arguments, ParsedEvent& result) -> void;
    auto emit_tool_finish(std::string_view key, ParsedEvent& result) -> void;
    auto flush_pending_tools() -> std::vector<ProviderEvent>;
};

} // namespace codeharness
