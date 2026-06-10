#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace codeharness::tui
{

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
    auto exit_loop() -> void;
    auto run(ftxui::Component component) -> void;

private:
    ftxui::ScreenInteractive screen_;
    TerminalAliveGate alive_;
};

} // namespace codeharness::tui
