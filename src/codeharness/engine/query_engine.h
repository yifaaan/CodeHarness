#pragma once

#include <absl/types/span.h>

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness::engine {
    // Input parameters for a model invocation.
    struct ApiMessageRequest {
        std::string model;
        // 历史消息
        std::vector<ConversationMessage> messages;
        std::string system_prompt;
        int max_tokens{4096};
        // 工具定义
        nlohmann::json tools;
    };

    // 模型最终的完整回复
    struct ApiMessageComplete {
        ConversationMessage message;
        UsageSnapshot usage;
    };

    // API streaming 返回的事件,
    //  1. 流式文本片段
    //  2. 完整消息结束
    using ApiStreamEvent = std::variant<AssistantTextDelta, ApiMessageComplete>;

    using StreamSink = std::function<void(const StreamEvent&)>;

    class ApiClient {
    public:
        virtual ~ApiClient() = default;

        virtual void stream_message(const ApiMessageRequest,
                                    std::function<void(const ApiStreamEvent&)> sink) = 0;
    };

    // 表示一次工具调用是否允许
    struct PermissionDecision {
        bool allowed{};
        bool requires_confirmation{};
        std::string reason;
    };

    // 调用工具前，用来执行权限检查
    class PerssionChecker {
    public:
        virtual ~PerssionChecker() = default;

        [[nodiscard]] virtual auto evaluate(absl::string_view tool_name, bool is_read_only,
                                            const nlohmann::json& input) const
            -> PermissionDecision = 0;
    };

    class QueryEngine {
    public:
        QueryEngine(ApiClient& api, const tools::ToolRegistry& tools,
                    const PerssionChecker& permissions, std::filesystem::path cwd,
                    std::string model, std::string system_prompt);

        // 用户发消息入口：
        //   把用户 prompt 加入 messages_
        //   调用 api_.stream_message(...)
        //   接收 assistant 的流式文本, 通过 sink 回调通知外部组件
        //   如果模型返回 tool use，就执行工具
        //   把 tool result 再塞回对话
        //   继续循环，直到模型不再请求工具
        auto submit_message(std::string prompt, const StreamSink& sink);

    private:
        // 负责单次工具调用：找工具、查权限、执行工具、包装成 ToolResultBlock
        auto execute_tool_call(const ToolUseBlock& call) -> ToolResultBlock;

        [[nodiscard]] auto messages() const noexcept -> absl::Span<const ConversationMessage>;

        [[nodiscard]] auto total_usage() const noexcept -> UsageSnapshot;

        auto clear() noexcept -> void;
        auto set_model(std::string model) noexcept -> void;
        auto set_system_prompt(std::string system_prompt) noexcept -> void;

        ApiClient& api_;
        const tools::ToolRegistry& tools_;
        const PerssionChecker& permissions_;
        std::filesystem::path cwd_;
        std::string model_;
        std::string system_prompt_;
        int max_tokens_{4096};
        int max_turns_{20};
        std::vector<ConversationMessage> messages_;  // 会话历史
        UsageSnapshot total_usage_;
    };
}  // namespace codeharness::engine