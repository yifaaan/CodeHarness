#include "codeharness/tui/terminal.h"

#include <ftxui/component/event.hpp>

#include <iostream>
#include <utility>
#include <chrono>

namespace codeharness::tui
{

auto disable_mouse_motion_tracking_sequence() -> std::string_view
{
    return "\x1B[?1003l";
}

TerminalAliveGate::TerminalAliveGate()
    : alive_{std::make_shared<std::atomic<bool>>(true)}
{
}

TerminalAliveGate::TerminalAliveGate(std::shared_ptr<std::atomic<bool>> alive)
    : alive_{std::move(alive)}
{
}

auto TerminalAliveGate::flag() const -> std::shared_ptr<std::atomic<bool>>
{
    return alive_;
}

auto TerminalAliveGate::is_alive() const -> bool
{
    return alive_->load(std::memory_order_acquire);
}

auto TerminalAliveGate::close() -> void
{
    alive_->store(false, std::memory_order_release);
}

auto TerminalAliveGate::post_if_alive(const std::function<void()>& post) const -> bool
{
    if (!is_alive())
    {
        return false;
    }
    post();
    return true;
}

TuiTerminalSession::TuiTerminalSession()
    : screen_{ftxui::ScreenInteractive::Fullscreen()}
{
}

TuiTerminalSession::~TuiTerminalSession()
{
    stop_animation_timer();
    close();
}

auto TuiTerminalSession::screen() -> ftxui::ScreenInteractive&
{
    return screen_;
}

auto TuiTerminalSession::alive_flag() const -> std::shared_ptr<std::atomic<bool>>
{
    return alive_.flag();
}

auto TuiTerminalSession::is_alive() const -> bool
{
    return alive_.is_alive();
}

auto TuiTerminalSession::close() -> void
{
    alive_.close();
}

auto TuiTerminalSession::post_refresh() -> void
{
    alive_.post_if_alive([&] { screen_.PostEvent(ftxui::Event::Custom); });
}

auto TuiTerminalSession::start_animation_timer(int interval_ms) -> void
{
    stop_animation_timer();
    animation_active_.store(true, std::memory_order_release);
    animation_timer_thread_ = std::thread{[this, interval_ms] {
        while (animation_active_.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            if (!animation_active_.load(std::memory_order_acquire))
            {
                break;
            }
            alive_.post_if_alive([&] { screen_.PostEvent(ftxui::Event::Custom); });
        }
    }};
}

auto TuiTerminalSession::stop_animation_timer() -> void
{
    animation_active_.store(false, std::memory_order_release);
    if (animation_timer_thread_.joinable())
    {
        animation_timer_thread_.join();
    }
}

auto TuiTerminalSession::exit_loop() -> void
{
    screen_.ExitLoopClosure()();
}

auto TuiTerminalSession::run(ftxui::Component component) -> void
{
    auto child = std::move(component);
    auto mouse_motion_disabled = std::make_shared<bool>(false);
    auto wrapped = ftxui::Renderer(child, [child, mouse_motion_disabled] {
        if (!*mouse_motion_disabled)
        {
            const auto sequence = disable_mouse_motion_tracking_sequence();
            std::cout.write(sequence.data(), static_cast<std::streamsize>(sequence.size()));
            std::cout << std::flush;
            *mouse_motion_disabled = true;
        }
        return child->Render();
    });

    screen_.Loop(std::move(wrapped));
    close();
}

} // namespace codeharness::tui
