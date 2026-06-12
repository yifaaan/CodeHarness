#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string_view>
#include <thread>

namespace codeharness::tui
{

[[nodiscard]] auto disable_mouse_motion_tracking_sequence() -> std::string_view;

class TerminalAliveGate
{
public:
    TerminalAliveGate();
    explicit TerminalAliveGate(std::shared_ptr<std::atomic<bool>> alive);

    [[nodiscard]] auto flag() const -> std::shared_ptr<std::atomic<bool>>;
    [[nodiscard]] auto is_alive() const -> bool;
    auto close() -> void;
    auto post_if_alive(const std::function<void()>& post) const -> bool;

private:
    std::shared_ptr<std::atomic<bool>> alive_;
};

class TuiTerminalSession
{
public:
    TuiTerminalSession();

    TuiTerminalSession(const TuiTerminalSession&) = delete;
    auto operator=(const TuiTerminalSession&) -> TuiTerminalSession& = delete;

    TuiTerminalSession(TuiTerminalSession&&) = delete;
    auto operator=(TuiTerminalSession&&) -> TuiTerminalSession& = delete;

    ~TuiTerminalSession();

    [[nodiscard]] auto screen() -> ftxui::ScreenInteractive&;
    [[nodiscard]] auto alive_flag() const -> std::shared_ptr<std::atomic<bool>>;
    [[nodiscard]] auto is_alive() const -> bool;

    auto close() -> void;
    auto post_refresh() -> void;
    auto start_animation_timer(int interval_ms = 80) -> void;
    auto stop_animation_timer() -> void;
    auto exit_loop() -> void;
    auto run(ftxui::Component component) -> void;

private:
    ftxui::ScreenInteractive screen_;
    TerminalAliveGate alive_;
    std::thread animation_timer_thread_;
    std::atomic<bool> animation_active_{false};
};

} // namespace codeharness::tui
