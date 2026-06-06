#include "codeharness/tui/tui_app.h"

#include "codeharness/core/overloaded.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <nonstd/expected.hpp>

#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace codeharness::tui
{

namespace
{

auto trim_to_width(std::string text, int width) -> std::string
{
    if (width > 0 && static_cast<int>(text.size()) > width)
    {
        text.resize(static_cast<std::size_t>(width));
    }
    return text;
}

auto prompt_detail_text(const PermissionPrompt& prompt) -> std::string
{
    std::ostringstream output;
    output << prompt.tool_name << ": " << prompt.reason;
    if (prompt.command)
    {
        output << "\ncommand: " << *prompt.command;
    }
    if (prompt.path)
    {
        output << "\npath: " << prompt.path->string();
    }
    return output.str();
}

} // namespace

auto TuiAppModel::state() const noexcept -> const TuiState&
{
    return state_;
}

auto TuiAppModel::render_text(int width) const -> std::string
{
    std::ostringstream output;
    output << "CodeHarness\n";
    for (const auto& item : state_.transcript)
    {
        output << item.kind << ": " << trim_to_width(item.text, width) << '\n';
    }
    if (state_.pending_permission)
    {
        output << "Permission required\n";
        output << prompt_detail_text(*state_.pending_permission) << '\n';
        output << "A approve · D deny\n";
    }
    output << (state_.busy ? "Running" : "Ready") << "\n> " << state_.composer;
    return output.str();
}

auto TuiAppModel::set_composer(std::string value) -> void
{
    state_.composer = std::move(value);
}

auto TuiAppModel::handle_submit() -> TuiAction
{
    if (state_.busy || state_.composer.empty() || state_.pending_permission)
    {
        return TuiAction::None;
    }
    return TuiAction::SubmitPrompt;
}

auto TuiAppModel::handle_quit() -> TuiAction
{
    if (state_.busy || state_.pending_permission)
    {
        return TuiAction::None;
    }
    state_.should_quit = true;
    return TuiAction::Quit;
}

auto TuiAppModel::handle_permission_approve() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    return TuiAction::ApprovePermission;
}

auto TuiAppModel::handle_permission_deny() -> TuiAction
{
    if (!state_.pending_permission)
    {
        return TuiAction::None;
    }
    return TuiAction::DenyPermission;
}

auto TuiAppModel::begin_prompt(std::string prompt) -> void
{
    state_.transcript.push_back(TranscriptItem{.kind = "user", .text = std::move(prompt)});
    state_.composer.clear();
    state_.busy = true;
}

auto TuiAppModel::complete_prompt() -> void
{
    state_.busy = false;
}

auto TuiAppModel::apply_engine_event(const EngineEvent& event) -> void
{
    std::visit(
        Overloaded{
            [this](const EngineAssistantTextDelta& delta) {
                if (!state_.transcript.empty() && state_.transcript.back().kind == "assistant")
                {
                    state_.transcript.back().text += delta.text;
                    return;
                }
                state_.transcript.push_back(TranscriptItem{.kind = "assistant", .text = delta.text});
            },
            [this](const EngineToolStarted& started) {
                state_.transcript.push_back(TranscriptItem{.kind = "tool", .text = "started " + started.name});
            },
            [this](const EngineToolFinished& finished) {
                state_.transcript.push_back(TranscriptItem{.kind = "tool", .text = "completed " + finished.id});
            },
            [this](const EngineToolResult& result) {
                state_.transcript.push_back(
                    TranscriptItem{.kind = "tool_result", .text = result.content, .is_error = result.is_error});
            },
            [this](const EngineError& error) {
                state_.transcript.push_back(TranscriptItem{.kind = "error", .text = error.message, .is_error = true});
            },
        },
        event);
}

auto TuiAppModel::show_permission(const PermissionPrompt& prompt) -> void
{
    state_.pending_permission = prompt;
}

auto TuiAppModel::clear_permission() -> void
{
    state_.pending_permission.reset();
}

auto run_tui(runtime::RuntimeBundle& runtime, int max_turns) -> Result<int>
{
    using namespace ftxui;

    auto screen = ScreenInteractive::TerminalOutput();
    TuiAppModel model;
    std::string input;

    std::mutex mutex;
    std::condition_variable permission_cv;
    std::optional<PermissionResponse> permission_response;

    auto run_prompt = [&](std::string prompt) {
        model.begin_prompt(prompt);
        std::thread worker{[&, prompt = std::move(prompt)] {
            auto result = runtime.run_prompt(
                prompt,
                runtime::RunPromptOptions{
                    .max_turns = max_turns,
                    .permission_prompt =
                        [&](const PermissionPrompt& permission_prompt) -> Result<PermissionResponse> {
                        {
                            std::lock_guard lock{mutex};
                            permission_response.reset();
                            model.show_permission(permission_prompt);
                        }
                        screen.PostEvent(Event::Custom);

                        std::unique_lock lock{mutex};
                        permission_cv.wait(lock, [&] { return permission_response.has_value(); });
                        auto response = *permission_response;
                        permission_response.reset();
                        model.clear_permission();
                        screen.PostEvent(Event::Custom);
                        return response;
                    },
                },
                [&](const EngineEvent& event) {
                    std::lock_guard lock{mutex};
                    model.apply_engine_event(event);
                    screen.PostEvent(Event::Custom);
                });

            {
                std::lock_guard lock{mutex};
                if (!result)
                {
                    model.apply_engine_event(EngineError{.message = result.error().message});
                }
                model.complete_prompt();
            }
            screen.PostEvent(Event::Custom);
        }};
        worker.detach();
    };

    auto input_component = Input(&input, "Ask CodeHarness");
    auto component = CatchEvent(input_component, [&](Event event) {
        {
            std::lock_guard lock{mutex};
            if (model.state().pending_permission)
            {
                if (event == Event::Character("a") || event == Event::Character("A"))
                {
                    permission_response = PermissionResponse{.allowed = true};
                    permission_cv.notify_one();
                    return true;
                }
                if (event == Event::Character("d") || event == Event::Character("D") || event == Event::Escape)
                {
                    permission_response = PermissionResponse{.allowed = false, .reason = "user denied permission"};
                    permission_cv.notify_one();
                    return true;
                }
                return true;
            }
        }

        if (event == Event::CtrlC || event == Event::CtrlD)
        {
            std::lock_guard lock{mutex};
            if (model.handle_quit() == TuiAction::Quit)
            {
                screen.ExitLoopClosure()();
            }
            return true;
        }

        if (event == Event::Return)
        {
            {
                std::lock_guard lock{mutex};
                model.set_composer(input);
                if (model.handle_submit() != TuiAction::SubmitPrompt)
                {
                    return true;
                }
            }
            auto prompt = input;
            input.clear();
            run_prompt(std::move(prompt));
            return true;
        }

        return false;
    });

    auto renderer = Renderer(component, [&] {
        std::lock_guard lock{mutex};

        Elements rows;
        rows.push_back(text("CodeHarness") | bold);
        rows.push_back(separator());
        for (const auto& item : model.state().transcript)
        {
            auto row = text(item.kind + ": " + item.text);
            rows.push_back(item.is_error ? row | color(Color::Red) : row);
        }
        rows.push_back(filler());

        if (model.state().pending_permission)
        {
            const auto& prompt = *model.state().pending_permission;
            rows.push_back(separator());
            rows.push_back(text("Permission required") | bold | color(Color::Yellow));
            rows.push_back(text(prompt.tool_name + ": " + prompt.reason));
            if (prompt.command)
            {
                rows.push_back(text("command: " + *prompt.command));
            }
            if (prompt.path)
            {
                rows.push_back(text("path: " + prompt.path->string()));
            }
            rows.push_back(text("A approve · D deny") | dim);
        }

        rows.push_back(separator());
        rows.push_back(hbox({text(model.state().busy ? "Running " : "Ready "), input_component->Render()}));
        return vbox(std::move(rows)) | border;
    });

    screen.Loop(renderer);
    return 0;
}

} // namespace codeharness::tui
