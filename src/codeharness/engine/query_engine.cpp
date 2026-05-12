#include "query_engine.h"

#include <utility>

#include "message.h"
#include "stream_event.h"

namespace codeharness::engine {
    QueryEngine::QueryEngine(ApiClient& api, const ToolRegistry& tools,
                             const PerssionChecker& permissions, std::filesystem::path cwd,
                             std::string model, std::string system_prompt)
        : api_{api},
          tools_{tools},
          permissions_{permissions},
          cwd_{std::move(cwd)},
          model_{std::move(model)},
          system_prompt_{std::move(system_prompt)} {}

    auto QueryEngine::submit_message(std::string prompt, const StreamSink& sink) {
        messages_.push_back(ConversationMessage::from_user_text(std::move(prompt)));

        for (int turn = 0; turn < max_turns_; turn++) {
            ApiMessageComplete final_message;
            bool has_final_message{};

            const auto request = ApiMessageRequest{
                .model = model_,
                .messages = messages_,
                .system_prompt = system_prompt_,
                .max_tokens = max_tokens_,
                .tools = tools_.api_schema(),
            };

            api_.stream_message(request, [&](const ApiStreamEvent& event) {
                if (auto delta = std::get_if<AssistantTextDelta>(&event)) {
                    sink(*delta);
                    return;
                }
                if (auto complete = std::get_if<ApiMessageComplete>(&event)) {
                    final_message = *complete;
                    has_final_message = true;
                }
            });

            if (!has_final_message) {
                throw std::runtime_error{"model stream finished without a final message"};
            }

            // 保存 assistant 回复和用量
            messages_.push_back(final_message.message);
            total_usage_.input_tokens += final_message.usage.input_tokens;
            total_usage_.output_tokens += final_message.usage.output_tokens;

            // 发一个 assistant 这一轮完成的事件
            sink(AssistantTurnComplete{
                .message = final_message.message,
                .usage = final_message.usage,
            });

            // 检查工具调用的请求
            const auto tool_calls = final_message.message.tool_uses();
            if (tool_calls.empty()) {
                return;
            }

            std::vector<ContentBlock> tool_results;
            tool_results.reserve(tool_calls.size());

            for (const auto& call : tool_calls) {
                // 通知 UI 工具开始执行
                sink(ToolExecutionStared{
                    .tool_name = call.name,
                    .tool_input = call.input,
                });
                auto result = execute_tool_call(call);
                // 通知 UI 工具执行完成
                sink(ToolExecutionComplete{
                    .tool_name = call.name,
                    .output = result.content,
                    .is_error = result.is_error,
                });
                tool_results.emplace_back(std::move(result));
            }
            // 将工具执行结果作为user message添加到历史消息
            messages_.push_back(ConversationMessage{
                .role = MessageRole::user,
                .content = std::move(tool_results),
            });
        }
        throw std::runtime_error{"exceeded maximum turn limit"};
    }

}  // namespace codeharness::engine