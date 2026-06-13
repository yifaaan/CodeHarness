#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "codeharness/core/cancellation.h"
#include "codeharness/core/error.h"
#include "codeharness/core/message.h"
#include "codeharness/hooks/hook_executor.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness {

struct EngineOptions {
  int max_turns = 10;
};

struct PermissionPrompt {
  std::string id;
  std::string tool_use_id;
  std::string tool_name;
  std::string reason;
  std::optional<std::filesystem::path> path;
  std::optional<std::string> command;
};

struct PermissionResponse {
  bool allowed = false;
  std::string reason;
  bool remember_session = false;
};

using PermissionPromptHandler = std::function<absl::StatusOr<PermissionResponse>(const PermissionPrompt&)>;

struct UserQuestionPrompt {
  std::string id;
  std::string tool_use_id;
  std::string question;
  std::string reason;
};

struct UserQuestionResponse {
  std::string answer;
};

using UserQuestionHandler = std::function<absl::StatusOr<UserQuestionResponse>(const UserQuestionPrompt&)>;

struct RunRequest {
  std::string prompt;
  std::optional<std::string> system_prompt;
  std::optional<std::vector<Message>> initial_messages;
  PermissionPromptHandler permission_prompt;
  UserQuestionHandler user_question;
  CancellationToken cancellation;
  EngineOptions options;
};

struct RunResult {
  std::vector<Message> messages;
  std::string output_text;
  ProviderUsage usage;
};

struct EngineAssistantTextDelta {
  std::string text;
};

struct EngineToolStarted {
  std::string id;
  std::string name;
};

struct EngineToolInputDelta {
  std::string id;
  std::string input_json_delta;
};

struct EngineToolFinished {
  std::string id;
};

struct EngineToolResult {
  std::string id;
  std::string content;
  bool is_error = false;
};

struct EngineError {
  std::string message;
};

using EngineEvent = std::variant<EngineAssistantTextDelta, EngineToolStarted, EngineToolInputDelta, EngineToolFinished,
                                 EngineToolResult, EngineError>;

using EngineEventSink = std::function<void(const EngineEvent&)>;

class Engine {
 public:
  Engine(ChatProvider& provider, ToolRegistry& tools, const PermissionChecker* permissions = nullptr,
         const HookExecutor* hooks = nullptr, PermissionPromptHandler permission_prompt = {},
         UserQuestionHandler user_question = {});

  absl::StatusOr<RunResult> Run(const RunRequest& request);
  absl::StatusOr<RunResult> RunStreaming(const RunRequest& request, const EngineEventSink& sink);

  void SetPermissionChecker(const PermissionChecker* checker) { permissions_ = checker; }
  void SetChatProvider(ChatProvider& provider) noexcept { chat_provider_ = &provider; }
  void SetSenderId(std::string id) { sender_id_ = std::move(id); }

 private:
  ChatProvider* chat_provider_ = nullptr;
  ToolRegistry& tools_;
  const PermissionChecker* permissions_ = nullptr;
  const HookExecutor* hooks_ = nullptr;
  PermissionPromptHandler permission_prompt_;
  UserQuestionHandler user_question_;
  std::string sender_id_;
};

}  // namespace codeharness
