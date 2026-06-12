#include "codeharness/provider/openai_serialize.h"

#include <nlohmann/json.hpp>

#include "codeharness/provider/provider_tool_schema.h"

namespace codeharness {

namespace {

void AppendTextInput(nlohmann::json& input, std::string role, std::string text) {
  if (!text.empty()) {
    input.push_back({{"role", std::move(role)}, {"content", std::move(text)}});
  }
}

}  // namespace

nlohmann::json SerializeOpenAIInput(std::span<const Message> messages) {
  auto input = nlohmann::json::array();

  for (const auto& message : messages) {
    switch (message.role) {
      case Role::kSystem:
        AppendTextInput(input, "system", CollectText(message));
        break;
      case Role::kUser:
        AppendTextInput(input, "user", CollectText(message));
        break;
      case Role::kAssistant:
        for (const auto& block : message.content) {
          if (const auto* text = std::get_if<TextBlock>(&block)) {
            AppendTextInput(input, "assistant", text->text);
          } else if (const auto* tool_use = std::get_if<ToolUseBlock>(&block)) {
            input.push_back({{"type", "function_call"},
                             {"call_id", tool_use->id},
                             {"name", tool_use->name},
                             {"arguments", parse_tool_input_json_or_empty_object(tool_use->input_json).dump()}});
          }
          // ThinkingBlock: skip for OpenAI (output-only reasoning_content)
        }
        break;
      case Role::kTool:
        for (const auto& block : message.content) {
          if (const auto* result = std::get_if<ToolResultBlock>(&block)) {
            input.push_back(
                {{"type", "function_call_output"}, {"call_id", result->tool_use_id}, {"output", result->content}});
          }
        }
        break;
    }
  }

  return input;
}

nlohmann::json SerializeOpenAITools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions) {
  auto tools = nlohmann::json::array();
  for (const auto& [name, description] : tool_descriptions) {
    tools.push_back({{"type", "function"},
                     {"name", name},
                     {"description", description},
                     {"parameters", loose_tool_input_schema()},
                     {"strict", false}});
  }
  return tools;
}

}  // namespace codeharness
