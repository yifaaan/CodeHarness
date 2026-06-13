#include "message_json.h"

#include <absl/status/status.h>
#include <fmt/format.h>

#include <string>

namespace codeharness::llm {

namespace {

std::string ConcatTextParts(const std::vector<ContentPart>& parts) {
  std::string result;
  for (const auto& part : parts) {
    if (auto* text = std::get_if<TextPart>(&part)) {
      if (!result.empty()) result += '\n';
      result += text->text;
    }
  }
  return result;
}

}  // namespace

nlohmann::json MessagesToJson(std::string_view system_prompt, std::span<const Message> messages) {
  auto arr = nlohmann::json::array();

  if (!system_prompt.empty()) {
    arr.push_back({{"role", "system"}, {"content", std::string(system_prompt)}});
  }

  for (const auto& msg : messages) {
    nlohmann::json obj;
    obj["role"] = msg.role == Role::kUser ? "user" : msg.role == Role::kAssistant ? "assistant" : "tool";

    if (msg.role == Role::kTool && msg.tool_call_id) {
      obj["tool_call_id"] = *msg.tool_call_id;
    }

    obj["content"] = ConcatTextParts(msg.content);

    if (msg.role == Role::kAssistant && !msg.tool_calls.empty()) {
      auto calls = nlohmann::json::array();
      for (const auto& tc : msg.tool_calls) {
        calls.push_back(
            {{"id", tc.id}, {"type", "function"}, {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}});
      }
      obj["tool_calls"] = calls;
    }

    arr.push_back(std::move(obj));
  }

  return arr;
}

nlohmann::json ToolsToJson(std::span<const Tool> tools) {
  auto arr = nlohmann::json::array();
  for (const auto& tool : tools) {
    arr.push_back({{"type", "function"},
                   {"function",
                    {{"name", tool.name},
                     {"description", tool.description},
                     {"parameters", tool.input_schema.is_null() ? nlohmann::json::object() : tool.input_schema}}}});
  }
  return arr;
}

absl::StatusOr<StreamChunk> ParseStreamChunk(const std::string& json_str) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (const nlohmann::json::parse_error& e) {
    return absl::InternalError(fmt::format("failed to parse SSE chunk: {}", e.what()));
  }

  StreamChunk chunk;

  if (j.contains("usage") && j["usage"].is_object()) {
    const auto& u = j["usage"];
    TokenUsage usage;
    usage.output = u.value("completion_tokens", 0);
    int64_t prompt = u.value("prompt_tokens", 0);
    int64_t cached = 0;
    if (u.contains("prompt_tokens_details") && u["prompt_tokens_details"].is_object()) {
      cached = u["prompt_tokens_details"].value("cached_tokens", 0);
    }
    usage.input_other = prompt - cached;
    usage.input_cache_read = cached;
    chunk.usage = usage;
  }

  if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
    return chunk;
  }

  const auto& choice = j["choices"][0];

  if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
    chunk.finish_reason = choice["finish_reason"].get<std::string>();
  }

  if (!choice.contains("delta") || !choice["delta"].is_object()) {
    return chunk;
  }

  const auto& delta = choice["delta"];

  if (delta.contains("content") && !delta["content"].is_null()) {
    chunk.content = delta["content"].get<std::string>();
  }

  if (delta.contains("tool_calls") && delta["tool_calls"].is_array() && !delta["tool_calls"].empty()) {
    const auto& tc = delta["tool_calls"][0];
    if (tc.contains("index")) chunk.tool_call_index = tc["index"].get<int>();
    if (tc.contains("id") && !tc["id"].is_null()) chunk.tool_call_id = tc["id"].get<std::string>();
    if (tc.contains("function") && tc["function"].is_object()) {
      const auto& fn = tc["function"];
      if (fn.contains("name") && !fn["name"].is_null()) chunk.tool_call_name = fn["name"].get<std::string>();
      if (fn.contains("arguments") && !fn["arguments"].is_null())
        chunk.tool_call_args = fn["arguments"].get<std::string>();
    }
  }

  return chunk;
}

FinishReason MapFinishReason(std::string_view reason) {
  if (reason == "stop") return FinishReason::kCompleted;
  if (reason == "tool_calls") return FinishReason::kToolCalls;
  if (reason == "length") return FinishReason::kTruncated;
  if (reason == "content_filter") return FinishReason::kFiltered;
  return FinishReason::kOther;
}

}  // namespace codeharness::llm
