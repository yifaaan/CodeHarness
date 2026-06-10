#include "codeharness/tui/tui_theme.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#include <cstring>
#endif

namespace codeharness::tui
{

namespace
{

/// Check if environment variable indicates true color support.
[[nodiscard]] auto detect_true_color_support() -> bool
{
#ifdef _WIN32
    // Windows 10+ supports true color
    OSVERSIONINFOEXW osvi{};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION,
                           VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL) |
                               VerSetConditionMask(0, VER_MINORVERSION, VER_GREATER_EQUAL)))
    {
        // Windows 10 build 14931+ supports true color
        return true;
    }
    return false;
#else
    // Check COLORTERM environment variable
    if (const auto* colorterm = std::getenv("COLORTERM"))
    {
        if (std::strcmp(colorterm, "truecolor") == 0 || std::strcmp(colorterm, "24bit") == 0)
        {
            return true;
        }
    }

    // Check TERM environment variable
    if (const auto* term = std::getenv("TERM"))
    {
        if (std::strstr(term, "truecolor") != nullptr || std::strstr(term, "24bit") != nullptr)
        {
            return true;
        }
        if (std::strstr(term, "256color") != nullptr)
        {
            return false;  // 256-color terminal
        }
    }

    // Default to true color for modern terminals
    return true;
#endif
}

/// Detect color level based on terminal capabilities.
[[nodiscard]] auto detect_color_level() -> ColorLevel
{
    // Check for forced color mode via environment
#ifdef _WIN32
    // Windows console often supports true color now
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode))
        {
            // Windows 10+ with virtual terminal processing
            if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
            {
                return ColorLevel::TrueColor;
            }
        }
    }
    return ColorLevel::Ansi256;
#else
    if (const auto* no_color = std::getenv("NO_COLOR"))
    {
        return ColorLevel::Ansi16;
    }

    if (detect_true_color_support())
    {
        return ColorLevel::TrueColor;
    }

    if (const auto* term = std::getenv("TERM"))
    {
        if (std::strstr(term, "256color") != nullptr)
        {
            return ColorLevel::Ansi256;
        }
    }

    // Check for common modern terminals that support true color
    if (const auto* term_program = std::getenv("TERM_PROGRAM"))
    {
        if (std::strcmp(term_program, "iTerm.app") == 0 ||
            std::strcmp(term_program, "vscode") == 0 ||
            std::strcmp(term_program, "Apple_Terminal") == 0)
        {
            return ColorLevel::TrueColor;
        }
    }

    return ColorLevel::Ansi256;
#endif
}

/// Try to detect terminal background color (simplified).
/// Returns nullopt if unable to determine.
[[nodiscard]] auto detect_terminal_bg() -> std::optional<RgbColor>
{
    // This is a simplified implementation.
    // In practice, detecting terminal background requires querying the terminal
    // using OSC escape sequences, which is complex and terminal-dependent.

    // Check for common dark/light theme indicators
#ifdef _WIN32
    // On Windows, we could check the registry for system theme
    // For now, default to dark
    return RgbColor{30, 30, 30};  // Assume dark background
#else
    // Check COLORFGBG environment variable (used by some terminals)
    if (const auto* colorfgbg = std::getenv("COLORFGBG"))
    {
        // Format is typically "fg;bg" where numbers are color indices
        // bg=0 usually means black (dark), bg=15 or 7 means light
        const auto* bg_str = std::strchr(colorfgbg, ';');
        if (bg_str != nullptr)
        {
            const auto bg_val = std::atoi(bg_str + 1);
            if (bg_val == 0 || bg_val == 8)  // Black or dark gray
            {
                return RgbColor{30, 30, 30};  // Dark background
            }
            if (bg_val == 7 || bg_val == 15)  // White or light gray
            {
                return RgbColor{250, 250, 250};  // Light background
            }
        }
    }

    // Default to dark theme
    return RgbColor{30, 30, 30};
#endif
}

/// Try to detect terminal foreground color.
[[nodiscard]] auto detect_terminal_fg() -> std::optional<RgbColor>
{
#ifdef _WIN32
    return RgbColor{220, 220, 220};  // Light text for dark background
#else
    // Check COLORFGBG for foreground
    if (const auto* colorfgbg = std::getenv("COLORFGBG"))
    {
        const auto fg_val = std::atoi(colorfgbg);
        if (fg_val == 0 || fg_val == 8)
        {
            return RgbColor{30, 30, 30};  // Dark text
        }
        if (fg_val == 7 || fg_val == 15)
        {
            return RgbColor{250, 250, 250};  // Light text
        }
    }

    return RgbColor{220, 220, 220};  // Light text for dark background
#endif
}

} // namespace

auto TerminalPalette::detect() -> TerminalPalette
{
    TerminalPalette palette;

    palette.color_level = detect_color_level();
    palette.default_bg = detect_terminal_bg();
    palette.default_fg = detect_terminal_fg();

    // Determine if dark theme based on background color
    if (palette.default_bg)
    {
        palette.is_dark_theme = !is_light(*palette.default_bg);
    }
    else
    {
        palette.is_dark_theme = true;  // Default to dark theme
    }

    return palette;
}

} // namespace codeharness::tui
