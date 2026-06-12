#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/network/sse_parser.h"
#include "codeharness/provider/provider.h"

namespace codeharness {

class OpenAIStreamParser {
 public:
  struct ParsedEvent {
    std::vector<ProviderEvent> events;
    bool done = false;
    std::string error;
  };

  ParsedEvent Feed(std::string_view chunk);

 private:
  network::SseParser sse_;

  struct ToolAccum {
    std::string id;
    std::string name;
    std::string arguments;
    bool started = false;
    bool finished = false;
    bool streamed_arguments = false;
  };
  std::map<std::string, ToolAccum> pending_tool_calls_;
  bool finished_ = false;

  void HandleJsonEvent(const nlohmann::json& event, ParsedEvent& result);
  void EmitToolStart(std::string_view key, ParsedEvent& result);
  void EmitToolArguments(std::string_view key, std::string arguments, ParsedEvent& result);
  void EmitToolFinish(std::string_view key, ParsedEvent& result);
  std::vector<ProviderEvent> FlushPendingTools();
};

}  // namespace codeharness
