#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace codeharness {

enum class Role {
  kSystem,
  kUser,
  kAssistant,
  kTool,
};

struct TextBlock {
  std::string text;
};

struct ThinkingBlock {
  std::string text;
};

struct ToolUseBlock {
  std::string id;
  std::string name;
  std::string input_json;
};

struct ToolResultBlock {
  std::string tool_use_id;
  std::string content;
  bool is_error = false;
};

using ContentBlock = std::variant<TextBlock, ThinkingBlock, ToolUseBlock, ToolResultBlock>;

struct Message {
  Role role = Role::kUser;
  std::vector<ContentBlock> content;
};

// Factories.

inline Message MakeTextMessage(Role role, std::string text) {
  Message message;
  message.role = role;
  message.content.emplace_back(TextBlock{std::move(text)});
  return message;
}

inline Message MakeToolResultMessage(std::vector<ToolResultBlock> results) {
  Message message;
  message.role = Role::kTool;
  for (auto& result : results) {
    message.content.emplace_back(std::move(result));
  }
  return message;
}

inline ToolResultBlock MakeErrorToolResult(std::string_view tool_use_id, std::string error_message) {
  return ToolResultBlock{
      .tool_use_id = std::string(tool_use_id),
      .content = std::move(error_message),
      .is_error = true,
  };
}

// Accessors.

inline std::string CollectText(const Message& message) {
  std::string result;
  for (const auto& block : message.content) {
    if (auto* text_block = std::get_if<TextBlock>(&block)) {
      result += text_block->text;
    }
  }
  return result;
}

inline std::vector<ToolUseBlock> CollectToolUses(const Message& message) {
  std::vector<ToolUseBlock> result;
  for (auto& block : message.content) {
    if (auto* tool_use = std::get_if<ToolUseBlock>(&block)) {
      result.push_back(*tool_use);
    }
  }
  return result;
}

inline std::vector<ToolResultBlock> CollectToolResults(const Message& message) {
  std::vector<ToolResultBlock> result;
  for (auto& block : message.content) {
    if (auto* tool_result = std::get_if<ToolResultBlock>(&block)) {
      result.push_back(*tool_result);
    }
  }
  return result;
}

}  // namespace codeharness
