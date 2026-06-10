#include "codeharness/tui/tui_app.h"

#include "codeharness/core/overloaded.h"

namespace codeharness::tui
{

auto TuiAppModel::apply_tui_event(const TuiEvent& event, const TuiAppEventContext& context) -> TuiAppEventResult
{
    TuiAppEventResult result;

    std::visit(
        Overloaded{
            [&](const TuiEngineEvent& engine_event) {
                apply_engine_event(engine_event.event);
            },
            [&](const TuiRunCompleted& completed) {
                if (!completed.success)
                {
                    apply_engine_event(EngineError{.message = completed.error_message});
                }
                else if (!completed.output_text.empty() && !has_streamed_assistant_output())
                {
                    apply_engine_event(EngineAssistantTextDelta{.text = completed.output_text});
                }

                if (completed.success)
                {
                    result.token_usage = TokenUsage{
                        .input_tokens = completed.input_tokens,
                        .output_tokens = completed.output_tokens,
                    };
                }

                clear_permission();
                close_question();
                complete_prompt();
                set_permission_mode(context.permission_mode);
                set_active_session(context.active_session);

                result.clear_user_question_response = true;
                result.release_cancellation = true;
                result.run_completed = true;
            },
            [&](const TuiPermissionRequested& requested) {
                result.clear_permission_response = true;
                show_permission(requested.prompt);
            },
            [&](const TuiQuestionRequested& requested) {
                result.clear_user_question_response = true;
                show_question(
                    requested.prompt.id,
                    requested.prompt.question,
                    "ask_user",
                    requested.prompt.reason);
            },
            [&](const TuiRefreshRequested&) {
                result.refresh_requested = true;
            },
        },
        event);

    return result;
}

} // namespace codeharness::tui
