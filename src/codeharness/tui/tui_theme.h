#pragma once

#include <ftxui/screen/color.hpp>

#include <string_view>

namespace codeharness::tui
{

inline constexpr std::string_view k_select_pointer = "\xe2\x9d\xaf "; // ❯
inline constexpr std::string_view k_current_mark = " \xe2\x86\x90 current"; // ← current

struct TuiTheme
{
    static auto primary() -> ftxui::Color
    {
        return ftxui::Color::Cyan;
    }
    static auto accent() -> ftxui::Color
    {
        return ftxui::Color::Blue;
    }
    static auto text_strong() -> ftxui::Color
    {
        return ftxui::Color::White;
    }
    static auto text_dim() -> ftxui::Color
    {
        return ftxui::Color::GrayDark;
    }
    static auto text_muted() -> ftxui::Color
    {
        return ftxui::Color::GrayLight;
    }
    static auto success() -> ftxui::Color
    {
        return ftxui::Color::Green;
    }
    static auto warning() -> ftxui::Color
    {
        return ftxui::Color::Yellow;
    }
    static auto error() -> ftxui::Color
    {
        return ftxui::Color::Red;
    }
};

} // namespace codeharness::tui
