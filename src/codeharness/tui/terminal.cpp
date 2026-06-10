#include "codeharness/tui/terminal.h"

#include <ftxui/component/event.hpp>

#include <utility>

namespace codeharness::tui
{

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

auto TuiTerminalSession::exit_loop() -> void
{
    screen_.ExitLoopClosure()();
}

auto TuiTerminalSession::run(ftxui::Component component) -> void
{
    screen_.Loop(std::move(component));
    close();
}

} // namespace codeharness::tui
