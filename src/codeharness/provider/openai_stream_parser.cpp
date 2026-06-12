#include "codeharness/provider/openai_stream_parser.h"

#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/core/strings.h"
#include "codeharness/provider/provider_json_helpers.h"

namespace codeharness {

namespace {

std::string EventType(const nlohmann::json& input) { return json_string_value(input, "type"); }

std::string ErrorMessageFrom(const nlohmann::json& input) {
  if (auto found = input.find("error"); found != input.end() && found->is_object()) {
    auto message = json_string_value(*found, "message");
    if (!message.empty()) return message;
  }

  if (auto found = input.find("response"); found != input.end() && found->is_object()) {
    if (auto error = found->find("error"); error != found->end() && error->is_object()) {
      auto message = json_string_value(*error, "message");
      if (!message.empty()) return message;
    }
  }

  return "OpenAI response failed";
}

auto UsageObjectFromCompletedEvent(const nlohmann::json& event) -> const nlohmann::json* {
  if (auto usage = event.find("usage"); usage != event.end() && usage->is_object()) return &*usage;

  if (auto response = event.find("response"); response != event.end() && response->is_object()) {
    if (auto usage = response->find("usage"); usage != response->end() && usage->is_object()) return &*usage;
  }

  return nullptr;
}

std::string KeyForItem(const nlohmann::json& event, const nlohmann::json& item = nlohmann::json::object()) {
  auto key = json_string_value(event, "item_id");
  if (key.empty()) key = json_string_value(item, "id");
  if (key.empty()) key = json_string_value(item, "call_id");
  return key;
}

}  // namespace

OpenAIStreamParser::ParsedEvent OpenAIStreamParser::Feed(std::string_view chunk) {
  ParsedEvent result;

  for (const auto& event : sse_.feed(chunk)) {
    if (event.data == "[DONE]") {
      auto tool_events = FlushPendingTools();
      result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
      result.events.push_back(MessageFinished{});
      result.done = true;
      finished_ = true;
      continue;
    }

    try {
      auto json = nlohmann::json::parse(event.data);
      HandleJsonEvent(json, result);
    } catch (const nlohmann::json::exception& error) {
      bool recovered = false;
      std::size_t start = 0;
      while (start <= event.data.size()) {
        auto end = event.data.find('\n', start);
        auto line =
            StripTrailingCr(end == std::string::npos ? std::string_view{event.data}.substr(start)
                                                       : std::string_view{event.data}.substr(start, end - start));
        if (!line.empty()) {
          if (auto json = try_parse_json(line)) {
            HandleJsonEvent(*json, result);
            recovered = true;
            if (result.done) return result;
          }
        }
        if (end == std::string::npos) break;
        start = end + 1;
      }

      if (!recovered) {
        result.error = std::string{"OpenAI stream JSON parse error: "} + error.what();
        result.done = true;
        return result;
      }
    }
  }

  return result;
}

void OpenAIStreamParser::HandleJsonEvent(const nlohmann::json& event, ParsedEvent& result) {
  auto type = EventType(event);

  if (type == "error" || type == "response.failed" || type == "response.incomplete") {
    result.error = ErrorMessageFrom(event);
    result.done = true;
    finished_ = true;
    return;
  }

  if (type == "response.output_text.delta") {
    auto delta = json_string_value(event, "delta");
    if (!delta.empty()) {
      result.events.push_back(AssistantTextDelta{std::move(delta)});
    }
    return;
  }

  // Reasoning content (thinking) from OpenAI Responses API.
  if (type == "response.reasoning.delta") {
    auto delta = json_string_value(event, "delta");
    if (!delta.empty()) {
      result.events.push_back(ThinkingDelta{std::move(delta)});
    }
    return;
  }

  if (type == "response.output_item.added" || type == "response.output_item.done") {
    auto item = event.value("item", nlohmann::json::object());
    if (json_string_value(item, "type") != "function_call") return;

    auto key = KeyForItem(event, item);
    if (key.empty()) key = std::to_string(pending_tool_calls_.size());

    auto& tool = pending_tool_calls_[key];
    if (auto id = json_string_value(item, "call_id"); !id.empty()) tool.id = std::move(id);
    if (tool.id.empty()) tool.id = key;
    if (auto name = json_string_value(item, "name"); !name.empty()) tool.name = std::move(name);
    if (auto arguments = json_string_value(item, "arguments"); !arguments.empty() && !tool.streamed_arguments) {
      tool.arguments = std::move(arguments);
    }

    EmitToolStart(key, result);
    if (type == "response.output_item.done") {
      EmitToolFinish(key, result);
    }
    return;
  }

  if (type == "response.function_call_arguments.delta") {
    auto key = KeyForItem(event);
    if (key.empty()) key = std::to_string(pending_tool_calls_.size());

    auto& tool = pending_tool_calls_[key];
    if (tool.id.empty()) tool.id = key;
    EmitToolStart(key, result);

    auto delta = json_string_value(event, "delta");
    if (!delta.empty()) {
      tool.streamed_arguments = true;
      EmitToolArguments(key, std::move(delta), result);
    }
    return;
  }

  if (type == "response.function_call_arguments.done") {
    auto key = KeyForItem(event);
    if (key.empty()) return;

    auto& tool = pending_tool_calls_[key];
    if (auto arguments = json_string_value(event, "arguments"); !arguments.empty() && !tool.streamed_arguments) {
      tool.arguments = std::move(arguments);
    }
    EmitToolFinish(key, result);
    return;
  }

  if (type == "response.completed") {
    if (const auto* usage = UsageObjectFromCompletedEvent(event)) {
      result.events.push_back(usage->get<ProviderUsage>());
    }
    auto tool_events = FlushPendingTools();
    result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
    result.events.push_back(MessageFinished{});
    result.done = true;
    finished_ = true;
  }
}

void OpenAIStreamParser::EmitToolStart(std::string_view key, ParsedEvent& result) {
  auto found = pending_tool_calls_.find(std::string{key});
  if (found == pending_tool_calls_.end()) return;

  auto& tool = found->second;
  if (tool.started) return;

  if (tool.id.empty()) tool.id = std::string{key};
  tool.started = true;
  result.events.push_back(ToolUseStarted{.id = tool.id, .name = tool.name});
}

void OpenAIStreamParser::EmitToolArguments(std::string_view key, std::string arguments, ParsedEvent& result) {
  auto found = pending_tool_calls_.find(std::string{key});
  if (found == pending_tool_calls_.end()) return;

  EmitToolStart(key, result);
  if (!arguments.empty()) {
    result.events.push_back(ToolUseInputDelta{.id = found->second.id, .input_json_delta = std::move(arguments)});
  }
}

void OpenAIStreamParser::EmitToolFinish(std::string_view key, ParsedEvent& result) {
  auto found = pending_tool_calls_.find(std::string{key});
  if (found == pending_tool_calls_.end()) return;

  auto& tool = found->second;
  EmitToolStart(key, result);
  if (tool.finished) return;

  if (!tool.arguments.empty()) {
    EmitToolArguments(key, std::move(tool.arguments), result);
  }

  tool.finished = true;
  result.events.push_back(ToolUseFinished{.id = tool.id});
}

std::vector<ProviderEvent> OpenAIStreamParser::FlushPendingTools() {
  std::vector<ProviderEvent> events;

  for (auto& [key, tool] : pending_tool_calls_) {
    if (tool.finished) continue;

    ParsedEvent partial;
    EmitToolFinish(key, partial);
    events.insert(events.end(), partial.events.begin(), partial.events.end());
  }

  pending_tool_calls_.clear();
  return events;
}

}  // namespace codeharness
