//==============================================================================
// ui_backend.h — JSON Lines 后端协议
//
// 架构角色：通信协议层
// 职责：定义 codeharness 的"headless backend"模式下的 JSON Lines 通信
//       协议。这允许外部 UI（如 Claude Code 的 Web UI、IDE 插件等）
//       通过 stdin/stdout 与 codeharness 交互，而不是只依赖 CLI。
//
// 协议设计：
//   这是一个"后端"（backend）模式——codeharness 作为子进程启动，
//   通过 stdin 读取 JSON Lines 请求，通过 stdout 输出 JSON Lines 事件。
//
//   前端请求（FrontendRequest）：
//     {"type": "submit_line", "line": "write a test"}
//     {"type": "shutdown"}
//
//   后端事件（BackendEvent，每行前缀 OHJSON:）：
//     OHJSON:{"type":"ready"}
//     OHJSON:{"type":"assistant_delta","text":"Hello..."}
//     OHJSON:{"type":"tool_started","id":"txn1","name":"bash"}
//     OHJSON:{"type":"tool_result","id":"txn1","content":"...","is_error":false}
//     OHJSON:{"type":"line_complete"}
//     OHJSON:{"type":"error","message":"..."}
//     OHJSON:{"type":"shutdown"}
//
//   OHJSON: 前缀的作用是让 stdout 混合输出和事件流时可以区分
//   （codeharness 的 stdout 也可能有 log 输出）。
//
// BackendHost 是协议的服务端实现：
//   1. 接收 FrontendRequest（来自 stdin 的 JSON Lines）
//   2. 将 prompt 交给 RuntimeBundle::run_prompt
//   3. 将 EngineEvent 转换为 BackendEvent，每行一个 JSON 输出
//
//   为什么需要这个协议？
//   终端 UI（TUI）和 CLI 交互都使用这同一个协议。
//   CLI 模式（普通 --prompt）在内部分子自身就是前端，只不过用的是事件
//   sink 直接打印文本，而非走 JSON Lines。
//==============================================================================

#include "codeharness/core/result.h"
#include "codeharness/runtime/runtime.h"

#include <iosfwd>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace codeharness::ui_backend
{

struct FrontendRequest
{
    std::string type;
    std::optional<std::string> line;
    std::optional<std::string> request_id;
    std::optional<bool> allowed;
    std::optional<bool> remember_session;
    std::optional<std::string> command;
    std::optional<std::string> args;
    std::optional<std::string> query;
    std::optional<std::string> answer;
};

struct BackendReady
{
};

struct BackendAssistantDelta
{
    std::string text;
};

struct BackendToolStarted
{
    std::string id;
    std::string name;
};

struct BackendToolCompleted
{
    std::string id;
};

struct BackendToolResult
{
    std::string id;
    std::string content;
    bool is_error = false;
};

struct BackendPermissionModal
{
    std::string id;
    std::string tool_use_id;
    std::string tool_name;
    std::string reason;
    std::optional<std::string> path;
    std::optional<std::string> command;
};

struct BackendCommandEntry
{
    std::string name;
    std::string description;
    std::vector<std::string> aliases;
    std::string invocation;
};

struct BackendSelectRequest
{
    std::vector<BackendCommandEntry> commands;
};

struct BackendLineComplete
{
};

struct BackendUserQuestionModal
{
    std::string id;
    std::string tool_use_id;
    std::string question;
    std::string reason;
};

struct BackendUsage
{
    int input_tokens = 0;
    int output_tokens = 0;
    int total_tokens = 0;
};

struct BackendError
{
    std::string message;
};

struct BackendShutdown
{
};

using BackendEvent = std::variant<BackendReady,
                                  BackendAssistantDelta,
                                  BackendToolStarted,
                                  BackendToolCompleted,
                                  BackendToolResult,
                                  BackendPermissionModal,
                                  BackendUserQuestionModal,
                                  BackendSelectRequest,
                                  BackendLineComplete,
                                  BackendUsage,
                                  BackendError,
                                  BackendShutdown>;

auto parse_frontend_request(std::string_view line) -> Result<FrontendRequest>;
auto format_backend_event(const BackendEvent& event) -> std::string;

class BackendHost
{
public:
    BackendHost(runtime::RuntimeBundle& runtime, std::istream& input, std::ostream& output, int max_turns);

    auto run() -> Result<void>;

private:
    auto emit(const BackendEvent& event) -> void;
    auto handle_request(const FrontendRequest& request) -> bool;
    auto handle_submit_line(std::string line) -> void;
    auto run_submit_line(std::string line) -> void;
    auto handle_select_command(const FrontendRequest& request) -> void;
    auto handle_apply_select_command(const FrontendRequest& request) -> void;
    auto handle_permission_response(const FrontendRequest& request) -> void;
    auto handle_user_question_response(const FrontendRequest& request) -> void;
    auto handle_interrupt() -> void;
    auto emit_engine_event(const EngineEvent& event) -> void;
    auto request_permission(const PermissionPrompt& prompt) -> Result<PermissionResponse>;
    auto request_user_question(const UserQuestionPrompt& prompt) -> Result<UserQuestionResponse>;
    auto next_frontend_request() -> Result<std::optional<FrontendRequest>>;
    auto has_active_run() const -> bool;
    auto reap_finished_run() -> void;
    auto wait_for_active_run() -> void;

    runtime::RuntimeBundle& runtime_;
    std::istream& input_;
    std::ostream& output_;
    int max_turns_ = 10;
    std::queue<FrontendRequest> queued_requests_;
    mutable std::mutex state_mutex_;
    std::mutex output_mutex_;
    std::condition_variable permission_cv_;
    std::condition_variable user_question_cv_;
    std::optional<PermissionPrompt> pending_permission_;
    std::optional<PermissionResponse> pending_permission_response_;
    std::optional<UserQuestionPrompt> pending_user_question_;
    std::optional<UserQuestionResponse> pending_user_question_response_;
    std::unique_ptr<CancellationSource> active_cancellation_;
    std::thread active_worker_;
    bool active_run_ = false;
    bool active_run_finished_ = false;
};

} // namespace codeharness::ui_backend
