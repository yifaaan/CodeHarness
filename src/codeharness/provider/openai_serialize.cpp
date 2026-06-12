#include "codeharness/provider/openai_serialize.h"

#include <nlohmann/json.hpp>

#include "codeharness/provider/provider_tool_schema.h"

namespace codeharness {

namespace {

auto append_text_input(nlohmann::json& input, std::string role, std::string text) -> void {
  if (!text.empty()) {
    input.push_back({
        {"role", std::move(role)},
        {"content", std::move(text)},
    });
  }
}

}  // namespace

auto serialize_openai_input(std::span<const Message> messages) -> nlohmann::json {
  auto input = nlohmann::json::array();

  for (const auto& message : messages) {
    switch (message.role) {
      case Role::kSystem:
        append_text_input(input, "system", CollectText(message));
        break;
      case Role::kUser:
        append_text_input(input, "user", CollectText(message));
        break;
      case Role::kAssistant:
        for (const auto& block : message.content) {
          if (const auto* text = std::get_if<TextBlock>(&block)) {
            append_text_input(input, "assistant", text->text);
          } else if (const auto* tool_use = std::get_if<ToolUseBlock>(&block)) {
            input.push_back({
                {"type", "function_call"},
                {"call_id", tool_use->id},
                {"name", tool_use->name},
                {"arguments", parse_tool_input_json_or_empty_object(tool_use->input_json).dump()},
            });
          }
        }
        break;
      case Role::kTool:
        for (const auto& block : message.content) {
          if (const auto* result = std::get_if<ToolResultBlock>(&block)) {
            input.push_back({
                {"type", "function_call_output"},
                {"call_id", result->tool_use_id},
                {"output", result->content},
            });
          }
        }
        break;
    }
  }

  return input;
}

auto serialize_openai_tools(const std::vector<std::pair<std::string, std::string>>& tool_descriptions)
    -> nlohmann::json {
  auto tools = nlohmann::json::array();
  for (const auto& [name, description] : tool_descriptions) {
    tools.push_back({
        {"type", "function"},
        {"name", name},
        {"description", description},
        {"parameters", loose_tool_input_schema()},
        {"strict", false},
    });
  }
  return tools;
}

}  // namespace codeharness
