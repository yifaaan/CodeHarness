#pragma once

#include "codeharness/network/sse_parser.h"
#include "codeharness/provider/provider.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness
{

class AnthropicStreamParser
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
    struct ToolAccum
    {
        std::string id;
        std::string name;
        bool started = false;
        bool finished = false;
    };

    network::SseParser sse_;
    std::map<int, ToolAccum> tool_blocks_;

    auto handle_json_event(const nlohmann::json& event, ParsedEvent& result) -> void;
    auto emit_tool_start(int index, ParsedEvent& result) -> void;
    auto emit_tool_finish(int index, ParsedEvent& result) -> void;
    auto flush_tools() -> std::vector<ProviderEvent>;
};

} // namespace codeharness
