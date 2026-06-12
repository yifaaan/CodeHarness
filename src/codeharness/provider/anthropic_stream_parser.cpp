#include "codeharness/provider/anthropic_stream_parser.h"

#include <nlohmann/json.hpp>

#include "codeharness/provider/provider_json_helpers.h"

namespace codeharness {

namespace {

int JsonInt(const nlohmann::json& input, std::string_view key, int fallback = 0) {
  auto found = input.find(key);
  if (found == input.end() || !found->is_number_integer()) return fallback;
  return found->get<int>();
}

std::string ErrorMessageFrom(const nlohmann::json& input) {
  if (auto error = input.find("error"); error != input.end() && error->is_object()) {
    auto message = json_string_value(*error, "message");
    if (!message.empty()) return message;
  }
  return "Anthropic response failed";
}

std::string UnsupportedEvent(std::string_view type) {
  if (type.empty()) return "Anthropic stream event is missing a type";
  return "unsupported Anthropic stream event: " + std::string{type};
}

const nlohmann::json* UsageObjectFromEvent(const nlohmann::json& event) {
  if (auto usage = event.find("usage"); usage != event.end() && usage->is_object()) return &*usage;
  if (auto message = event.find("message"); message != event.end() && message->is_object()) {
    if (auto usage = message->find("usage"); usage != message->end() && usage->is_object()) return &*usage;
  }
  return nullptr;
}

void ApplyUsageUpdate(ProviderUsage& current, const nlohmann::json& usage) {
  if (auto input = JsonInt(usage, "input_tokens"); input > 0) current.input_tokens = input;
  if (auto output = JsonInt(usage, "output_tokens"); output > 0) current.output_tokens = output;
  current.total_tokens = current.input_tokens + current.output_tokens;
}

}  // namespace

AnthropicStreamParser::ParsedEvent AnthropicStreamParser::Feed(std::string_view chunk) {
  ParsedEvent result;

  for (const auto& event : sse_.feed(chunk)) {
    try {
      auto json = nlohmann::json::parse(event.data);
      HandleJsonEvent(json, result);
      if (!result.error.empty()) {
        result.done = true;
        return result;
      }
    } catch (const nlohmann::json::exception& error) {
      result.error = std::string{"Anthropic stream JSON parse error: "} + error.what();
      result.done = true;
      return result;
    }
  }

  return result;
}

void AnthropicStreamParser::HandleJsonEvent(const nlohmann::json& event, ParsedEvent& result) {
  auto type = json_string_value(event, "type");

  if (type == "error") {
    result.error = ErrorMessageFrom(event);
    result.done = true;
    return;
  }

  if (type == "message_start" || type == "message_delta") {
    if (const auto* usage = UsageObjectFromEvent(event)) {
      ApplyUsageUpdate(usage_, *usage);
      result.events.push_back(usage_);
    }
    return;
  }

  if (type == "ping") return;

  if (type == "content_block_start") {
    auto index = JsonInt(event, "index");
    auto content_block = event.value("content_block", nlohmann::json::object());
    auto block_type = json_string_value(content_block, "type");

    if (block_type == "thinking") {
      thinking_block_active_ = true;
      auto text = json_string_value(content_block, "text");
      if (!text.empty()) {
        result.events.push_back(ThinkingDelta{std::move(text)});
      }
      return;
    }

    if (block_type == "tool_use") {
      auto& tool = tool_blocks_[index];
      tool.id = json_string_value(content_block, "id");
      tool.name = json_string_value(content_block, "name");
      if (tool.id.empty()) tool.id = std::to_string(index);
      EmitToolStart(index, result);
      return;
    }

    return;
  }

  if (type == "content_block_delta") {
    auto index = JsonInt(event, "index");
    auto delta = event.value("delta", nlohmann::json::object());
    auto delta_type = json_string_value(delta, "type");

    if (delta_type == "text_delta") {
      auto text = json_string_value(delta, "text");
      if (!text.empty()) {
        result.events.push_back(AssistantTextDelta{std::move(text)});
      }
      return;
    }

    if (delta_type == "thinking_delta") {
      auto text = json_string_value(delta, "text");
      if (!text.empty()) {
        result.events.push_back(ThinkingDelta{std::move(text)});
      }
      return;
    }

    if (delta_type == "input_json_delta") {
      auto& tool = tool_blocks_[index];
      if (tool.id.empty()) tool.id = std::to_string(index);
      EmitToolStart(index, result);

      auto partial = json_string_value(delta, "partial_json");
      if (!partial.empty()) {
        result.events.push_back(ToolUseInputDelta{.id = tool.id, .input_json_delta = std::move(partial)});
      }
      return;
    }

    result.error = UnsupportedEvent("content_block_delta." + delta_type);
    return;
  }

  if (type == "content_block_stop") {
    // If the stopping block was a thinking block, reset the flag.
    thinking_block_active_ = false;
    EmitToolFinish(JsonInt(event, "index"), result);
    return;
  }

  if (type == "message_stop") {
    auto tool_events = FlushTools();
    result.events.insert(result.events.end(), tool_events.begin(), tool_events.end());
    result.events.push_back(MessageFinished{});
    result.done = true;
    return;
  }

  result.error = UnsupportedEvent(type);
}

void AnthropicStreamParser::EmitToolStart(int index, ParsedEvent& result) {
  auto found = tool_blocks_.find(index);
  if (found == tool_blocks_.end()) return;

  auto& tool = found->second;
  if (tool.started) return;

  tool.started = true;
  result.events.push_back(ToolUseStarted{.id = tool.id, .name = tool.name});
}

void AnthropicStreamParser::EmitToolFinish(int index, ParsedEvent& result) {
  auto found = tool_blocks_.find(index);
  if (found == tool_blocks_.end()) return;

  auto& tool = found->second;
  EmitToolStart(index, result);
  if (tool.finished) return;

  tool.finished = true;
  result.events.push_back(ToolUseFinished{.id = tool.id});
}

std::vector<ProviderEvent> AnthropicStreamParser::FlushTools() {
  std::vector<ProviderEvent> events;

  for (auto& [index, tool] : tool_blocks_) {
    if (tool.finished) continue;

    ParsedEvent partial;
    EmitToolFinish(index, partial);
    events.insert(events.end(), partial.events.begin(), partial.events.end());
  }

  tool_blocks_.clear();
  return events;
}

}  // namespace codeharness
