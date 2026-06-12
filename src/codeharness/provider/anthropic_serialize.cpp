#include "codeharness/provider/anthropic_serialize.h"

#include <nlohmann/json.hpp>

#include "codeharness/provider/provider_tool_schema.h"

namespace codeharness {

namespace {

void AppendTextBlock(nlohmann::json& content, std::string text) {
  if (!text.empty()) {
    content.push_back({{"type", "text"}, {"text", std::move(text)}});
  }
}

}  // namespace

std::string SerializeAnthropicSystem(std::span<const Message> messages) {
  std::string system;
  for (const auto& message : messages) {
    if (message.role != Role::kSystem) continue;

    auto text = CollectText(message);
    if (text.empty()) continue;

    if (!system.empty()) system += "\n\n";
    system += text;
  }
  return system;
}

nlohmann::json SerializeAnthropicMessages(std::span<const Message> messages) {
  auto serialized = nlohmann::json::array();

  for (const auto& message : messages) {
    if (message.role == Role::kSystem) continue;

    auto content = nlohmann::json::array();
    auto role = std::string{"user"};

    switch (message.role) {
      case Role::kSystem:
        break;
      case Role::kUser:
        AppendTextBlock(content, CollectText(message));
        break;
      case Role::kAssistant:
        role = "assistant";
        for (const auto& block : message.content) {
          if (const auto* text = std::get_if<TextBlock>(&block)) {
            AppendTextBlock(content, text->text);
          } else if (const auto* tool_use = std::get_if<ToolUseBlock>(&block)) {
            content.push_back({{"type", "tool_use"},
                               {"id", tool_use->id},
                               {"name", tool_use->name},
                               {"input", parse_tool_input_json_or_empty_object(tool_use->input_json)}});
          }
          // ThinkingBlock: skip for Anthropic (output-only thinking)
        }
        break;
      case Role::kTool:
        for (const auto& block : message.content) {
          if (const auto* result = std::get_if<ToolResultBlock>(&block)) {
            nlohmann::json tool_result = {
                {"type", "tool_result"},
                {"tool_use_id", result->tool_use_id},
                {"content", result->content},
            };
            if (result->is_error) {
              tool_result["is_error"] = true;
            }
            content.push_back(std::move(tool_result));
          }
        }
        break;
    }

    if (!content.empty()) {
      serialized.push_back({{"role", std::move(role)}, {"content", std::move(content)}});
    }
  }

  return serialized;
}

nlohmann::json SerializeAnthropicTools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions) {
  auto tools = nlohmann::json::array();
  for (const auto& [name, description] : tool_descriptions) {
    tools.push_back({{"name", name}, {"description", description}, {"input_schema", loose_tool_input_schema()}});
  }
  return tools;
}

}  // namespace codeharness
