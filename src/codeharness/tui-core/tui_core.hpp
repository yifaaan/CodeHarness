#pragma once

#include <string>

namespace codeharness::tuicore {

enum class Pane {
    Chat,
    Diff,
    Tasks,
    Agents,
    Status,
    Jobs
};

enum class UiEventType {
    KeyPressed,
    PromptSubmitted,
    ResponseDelta,
    ToolStarted,
    ToolFinished,
    JobQueued,
    JobProgress,
    JobCompleted,
    ApprovalRequested,
    ApprovalResolved,
    PauseRequested,
    ResumeRequested,
    Tick
};

struct UiEvent {
    UiEventType type;
    std::string payload;
};

struct UiState {
    Pane active_pane{Pane::Chat};
    bool paused{false};
    std::string last_response_delta;
    std::string active_tool;
    size_t pending_tasks{0};
    size_t active_jobs{0};
    bool pending_approval{false};
    std::string status_line;
};

auto reduce(UiState state, const UiEvent& event) -> UiState;

} // namespace codeharness::tuicore
