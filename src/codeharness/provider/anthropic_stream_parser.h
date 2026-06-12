#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/network/sse_parser.h"
#include "codeharness/provider/provider.h"

namespace codeharness {

class AnthropicStreamParser {
 public:
  struct ParsedEvent {
    std::vector<ProviderEvent> events;
    bool done = false;
    std::string error;
  };

  ParsedEvent Feed(std::string_view chunk);

 private:
  struct ToolAccum {
    std::string id;
    std::string name;
    bool started = false;
    bool finished = false;
  };

  network::SseParser sse_;
  std::map<int, ToolAccum> tool_blocks_;
  bool thinking_block_active_ = false;
  ProviderUsage usage_;

  void HandleJsonEvent(const nlohmann::json& event, ParsedEvent& result);
  void EmitToolStart(int index, ParsedEvent& result);
  void EmitToolFinish(int index, ParsedEvent& result);
  std::vector<ProviderEvent> FlushTools();
};

}  // namespace codeharness
