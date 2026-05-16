#include "codeharness/engine/query_engine.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>

#include <utility>

#include "codeharness/api/client.h"
#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/logging.h"
#include "codeharness/tools/base.h"

namespace codeharness::engine {
    QueryEngine::QueryEngine(api::Client& api, const tools::ToolRegistry& tools,
                             const permissions::PermissionChecker& permissions,
                             std::filesystem::path cwd, std::string model,
                             std::string system_prompt)
        : api_{api},
          tools_{tools},
          permissions_{permissions},
          cwd_{std::move(cwd)},
          model_{std::move(model)},
          system_prompt_{std::move(system_prompt)} {}

    auto QueryEngine::submit_message(std::string prompt, const api::StreamSink& sink)
        -> absl::Status {
        CH_LOG_DEBUG("QueryEngine::submit_message", "prompt_chars={} existing_messages={} model={}",
                     prompt.size(), messages_.size(), model_);
        messages_.push_back(ConversationMessage::from_user_text(std::move(prompt)));

        for (int turn = 0; turn < max_turns_; turn++) {
            api::MessageComplete final_message;
            bool has_final_message{};

            CH_LOG_DEBUG("QueryEngine::submit_message",
                         "turn={} preparing request messages_before_copy={}", turn,
                         messages_.size());
            const auto tools_schema = tools_.api_schema();
            CH_LOG_DEBUG("QueryEngine::submit_message",
                         "turn={} prepared tool schema tools={}", turn, tools_schema.size());
            const auto request = api::MessageRequest{
                .model = model_,
                .messages = messages_,
                .system_prompt = system_prompt_,
                .max_tokens = max_tokens_,
                .tools = std::move(tools_schema),
            };
            CH_LOG_DEBUG("QueryEngine::submit_message",
                         "turn={} sending request messages={} tools={} max_tokens={}", turn,
                         request.messages.size(), request.tools.size(), request.max_tokens);

            const auto status = api_.stream_message(request, [&](const api::ApiStreamEvent& event) {
                if (auto delta = std::get_if<AssistantTextDelta>(&event)) {
                    CH_LOG_DEBUG("QueryEngine::submit_message",
                                 "turn={} received assistant delta chars={}", turn,
                                 delta->text.size());
                    sink(*delta);
                    return;
                }
                if (auto complete = std::get_if<api::MessageComplete>(&event)) {
                    CH_LOG_DEBUG(
                        "QueryEngine::submit_message",
                        "turn={} received final message blocks={} input_tokens={} "
                        "output_tokens={} stop_reason={}",
                        turn, complete->message.content.size(), complete->usage.input_tokens,
                        complete->usage.output_tokens, complete->stop_reason);
                    final_message = *complete;
                    has_final_message = true;
                }
            });
            if (!status.ok()) {
                return status;
            }

            if (!has_final_message) {
                return absl::InternalError("model stream finished without a final message");
            }

            messages_.push_back(final_message.message);
            total_usage_.input_tokens += final_message.usage.input_tokens;
            total_usage_.output_tokens += final_message.usage.output_tokens;

            sink(AssistantTurnComplete{
                .message = final_message.message,
                .usage = final_message.usage,
            });

            const auto tool_calls = final_message.message.tool_uses();
            if (tool_calls.empty()) {
                CH_LOG_DEBUG("QueryEngine::submit_message",
                             "turn={} completed without tool calls total_input_tokens={} "
                             "total_output_tokens={}",
                             turn, total_usage_.input_tokens, total_usage_.output_tokens);
                return absl::OkStatus();
            }
            CH_LOG_DEBUG("QueryEngine::submit_message", "turn={} tool_calls={}", turn,
                         tool_calls.size());

            std::vector<ContentBlock> tool_results;
            tool_results.reserve(tool_calls.size());

            for (const auto& call : tool_calls) {
                sink(ToolExecutionStared{
                    .tool_name = call.name,
                    .tool_input = call.input,
                });
                CH_LOG_DEBUG("QueryEngine::submit_message",
                             "turn={} executing tool name={} id={} input_bytes={}", turn,
                             call.name, call.id, call.input.dump().size());
                auto result = execute_tool_call(call);
                if (!result.ok()) {
                    return result.status();
                }
                CH_LOG_DEBUG("QueryEngine::submit_message",
                             "turn={} tool finished name={} id={} is_error={} output_chars={}",
                             turn, call.name, call.id, result->is_error, result->content.size());
                sink(ToolExecutionComplete{
                    .tool_name = call.name,
                    .output = result->content,
                    .is_error = result->is_error,
                });
                tool_results.emplace_back(std::move(*result));
            }

            messages_.push_back(ConversationMessage{
                .role = MessageRole::user,
                .content = std::move(tool_results),
            });
            CH_LOG_DEBUG("QueryEngine::submit_message",
                         "turn={} appended tool results messages={}", turn, messages_.size());
        }

        CH_LOG_ERROR("QueryEngine::submit_message", "exceeded maximum turn limit max_turns={}",
                     max_turns_);
        return absl::ResourceExhaustedError("exceeded maximum turn limit");
    }

    auto QueryEngine::execute_tool_call(const ToolUseBlock& call)
        -> absl::StatusOr<ToolResultBlock> {
        auto selected_tool = tools_.find(call.name);
        if (!selected_tool.ok()) {
            CH_LOG_WARN("QueryEngine::execute_tool_call", "unknown tool requested name={} id={}",
                        call.name, call.id);
            return ToolResultBlock{
                .tool_use_id = call.id,
                .content = absl::StrCat("Unknown tool: ", call.name),
                .is_error = true,
            };
        }

        auto* tool = *selected_tool;
        const auto decision = permissions_.evaluate(tool->name(), tool->is_read_only(call.input),
                                                    call.input);
        CH_LOG_DEBUG("QueryEngine::execute_tool_call",
                     "permission decision tool={} allowed={} requires_confirmation={} reason={}",
                     std::string{tool->name()}, decision.allowed,
                     decision.requires_confirmation, decision.reason);
        if (!decision.allowed) {
            return ToolResultBlock{
                .tool_use_id = call.id,
                .content = decision.reason.empty()
                               ? absl::StrCat("Permission denied for ", tool->name())
                               : decision.reason,
                .is_error = true,
            };
        }

        const auto result = tool->execute(call.input, tools::ToolExecutionContext{.cwd = cwd_});
        if (!result.ok()) {
            CH_LOG_DEBUG("QueryEngine::execute_tool_call",
                         "tool execution failed tool={} status={} message={}",
                         std::string{tool->name()},
                         static_cast<int>(result.status().code()),
                         result.status().message());
            return ToolResultBlock{
                .tool_use_id = call.id,
                .content = std::string{result.status().message()},
                .is_error = true,
            };
        }
        CH_LOG_DEBUG("QueryEngine::execute_tool_call",
                     "tool execution succeeded tool={} output_chars={}",
                     std::string{tool->name()}, result->size());

        return ToolResultBlock{
            .tool_use_id = call.id,
            .content = std::move(*result),
            .is_error = false,
        };
    }

    auto QueryEngine::messages() const noexcept -> absl::Span<const ConversationMessage> {
        return messages_;
    }

    auto QueryEngine::total_usage() const noexcept -> UsageSnapshot { return total_usage_; }

    auto QueryEngine::clear() noexcept -> void {
        CH_LOG_DEBUG("QueryEngine::clear", "messages={} input_tokens={} output_tokens={}",
                     messages_.size(), total_usage_.input_tokens, total_usage_.output_tokens);
        messages_.clear();
        total_usage_ = UsageSnapshot{};
    }

    auto QueryEngine::set_model(std::string model) noexcept -> void {
        CH_LOG_DEBUG("QueryEngine::set_model", "from={} to={}", model_, model);
        model_ = std::move(model);
    }

    auto QueryEngine::set_system_prompt(std::string system_prompt) noexcept -> void {
        CH_LOG_DEBUG("QueryEngine::set_system_prompt", "system_prompt_chars={}",
                     system_prompt.size());
        system_prompt_ = std::move(system_prompt);
    }

    auto QueryEngine::set_max_turns(int max_turns) noexcept -> void {
        CH_LOG_DEBUG("QueryEngine::set_max_turns", "from={} to={}", max_turns_, max_turns);
        max_turns_ = max_turns;
    }
}  // namespace codeharness::engine
