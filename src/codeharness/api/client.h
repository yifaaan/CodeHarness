#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"

namespace codeharness::api {
    // Input parameters for a model invocation.
    struct MessageRequest {
        std::string model;
        // 历史消息
        std::vector<engine::ConversationMessage> messages;
        std::string system_prompt;
        int max_tokens{4096};
        // 工具定义
        nlohmann::json tools;
    };

    // 模型最终的完整回复
    struct MessageComplete {
        engine::ConversationMessage message;
        engine::UsageSnapshot usage;
        std::string stop_reason{"end_turn"};
    };

    // API streaming 返回给QueryEngine的事件,
    //  1. 流式文本片段
    //  2. 完整消息结束
    using ApiStreamEvent = std::variant<engine::AssistantTextDelta, MessageComplete>;
    // Client 通知 QueryEngine 的回调
    using ApiStreamSink = std::function<void(const ApiStreamEvent&)>;

    // QueryEngine 通知 ui 的回调
    using StreamSink = std::function<void(const engine::StreamEvent&)>;

    class Client {
    public:
        virtual ~Client() = default;

        // QueryEngine 调用client.stream_message()
        virtual auto stream_message(const MessageRequest& request, ApiStreamSink sink) -> void = 0;
    };
}  // namespace codeharness::api
