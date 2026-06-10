#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace codeharness::tui
{

/// RGB color represented as a tuple of (R, G, B) components.
struct RgbColor
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    [[nodiscard]] constexpr auto tuple() const noexcept -> std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>
    {
        return {r, g, b};
    }

    [[nodiscard]] static constexpr auto from_rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept -> RgbColor
    {
        return RgbColor{red, green, blue};
    }
};

/// Determines if a background color is light based on luminance.
/// Uses the relative luminance formula from WCAG 2.0.
/// Y > 128 indicates a light background.
[[nodiscard]] inline auto is_light(RgbColor bg) noexcept -> bool
{
    // Y = 0.299*R + 0.587*G + 0.114*B (ITU-R BT.601)
    const auto y = 0.299F * static_cast<float>(bg.r) +
                   0.587F * static_cast<float>(bg.g) +
                   0.114F * static_cast<float>(bg.b);
    return y > 128.0F;
}

/// Blends a foreground color with a background color using alpha compositing.
/// The alpha parameter controls how much of the foreground is visible.
/// alpha = 1.0 means fully foreground, alpha = 0.0 means fully background.
[[nodiscard]] inline auto blend(RgbColor fg, RgbColor bg, float alpha) noexcept -> RgbColor
{
    alpha = std::clamp(alpha, 0.0F, 1.0F);
    const auto inv_alpha = 1.0F - alpha;

    return RgbColor{
        static_cast<std::uint8_t>(std::round(static_cast<float>(fg.r) * alpha + static_cast<float>(bg.r) * inv_alpha)),
        static_cast<std::uint8_t>(std::round(static_cast<float>(fg.g) * alpha + static_cast<float>(bg.g) * inv_alpha)),
        static_cast<std::uint8_t>(std::round(static_cast<float>(fg.b) * alpha + static_cast<float>(bg.b) * inv_alpha)),
    };
}

/// Converts sRGB component to linear RGB.
[[nodiscard]] inline auto srgb_to_linear(std::uint8_t c) noexcept -> float
{
    const auto v = static_cast<float>(c) / 255.0F;
    if (v <= 0.04045F)
    {
        return v / 12.92F;
    }
    return std::pow((v + 0.055F) / 1.055F, 2.4F);
}

/// Converts linear RGB component to sRGB.
[[nodiscard]] inline auto linear_to_srgb(float c) noexcept -> std::uint8_t
{
    c = std::clamp(c, 0.0F, 1.0F);
    if (c <= 0.0031308F)
    {
        return static_cast<std::uint8_t>(std::round(255.0F * 12.92F * c));
    }
    return static_cast<std::uint8_t>(std::round(255.0F * (1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F)));
}

/// Converts RGB to XYZ color space.
[[nodiscard]] inline auto rgb_to_xyz(RgbColor color) noexcept -> std::tuple<float, float, float>
{
    const auto r = srgb_to_linear(color.r);
    const auto g = srgb_to_linear(color.g);
    const auto b = srgb_to_linear(color.b);

    const auto x = r * 0.4124F + g * 0.3576F + b * 0.1805F;
    const auto y = r * 0.2126F + g * 0.7152F + b * 0.0722F;
    const auto z = r * 0.0193F + g * 0.1192F + b * 0.9505F;

    return {x, y, z};
}

/// Converts XYZ to Lab color space (D65 reference white).
[[nodiscard]] inline auto xyz_to_lab(float x, float y, float z) noexcept -> std::tuple<float, float, float>
{
    // D65 reference white point
    constexpr auto x_ref = 0.95047F;
    constexpr auto y_ref = 1.00000F;
    constexpr auto z_ref = 1.08883F;

    const auto xr = x / x_ref;
    const auto yr = y / y_ref;
    const auto zr = z / z_ref;

    const auto f = [](float t) noexcept -> float {
        constexpr auto delta = 6.0F / 29.0F;
        if (t > delta * delta * delta)
        {
            return std::cbrt(t);
        }
        return t / (3.0F * delta * delta) + 4.0F / 29.0F;
    };

    const auto fx = f(xr);
    const auto fy = f(yr);
    const auto fz = f(zr);

    const auto l = 116.0F * fy - 16.0F;
    const auto a = 500.0F * (fx - fy);
    const auto b = 200.0F * (fy - fz);

    return {l, a, b};
}

/// Calculates perceptual color distance using CIE76 (Euclidean distance in Lab space).
/// This provides a more accurate measure of perceived color difference than simple RGB distance.
[[nodiscard]] inline auto perceptual_distance(RgbColor a, RgbColor b) noexcept -> float
{
    const auto [x1, y1, z1] = rgb_to_xyz(a);
    const auto [x2, y2, z2] = rgb_to_xyz(b);

    const auto [l1, a1, b1] = xyz_to_lab(x1, y1, z1);
    const auto [l2, a2, b2] = xyz_to_lab(x2, y2, z2);

    const auto dl = l1 - l2;
    const auto da = a1 - a2;
    const auto db = b1 - b2;

    return std::sqrt(dl * dl + da * da + db * db);
}

/// Finds the best 256-color approximation for a given RGB color.
/// Uses perceptual distance for better visual matching.
[[nodiscard]] inline auto best_256_color(RgbColor target) noexcept -> std::uint8_t
{
    // Standard 256-color palette (6x6x6 color cube at indices 16-231)
    // Plus 24 grayscale colors at indices 232-255
    std::uint8_t best_index = 0;
    auto best_distance = std::numeric_limits<float>::max();

    // Check grayscale ramp (232-255)
    for (std::uint8_t i = 232; i <= 255; ++i)
    {
        const auto gray = static_cast<std::uint8_t>(8 + (i - 232) * 10);
        const RgbColor gray_color{gray, gray, gray};
        const auto dist = perceptual_distance(target, gray_color);
        if (dist < best_distance)
        {
            best_distance = dist;
            best_index = i;
        }
    }

    // Check 6x6x6 color cube (16-231)
    for (int r = 0; r < 6; ++r)
    {
        for (int g = 0; g < 6; ++g)
        {
            for (int b = 0; b < 6; ++b)
            {
                const auto index = static_cast<std::uint8_t>(16 + 36 * r + 6 * g + b);
                const auto red = static_cast<std::uint8_t>((r > 0) ? (55 + 40 * r) : 0);
                const auto green = static_cast<std::uint8_t>((g > 0) ? (55 + 40 * g) : 0);
                const auto blue = static_cast<std::uint8_t>((b > 0) ? (55 + 40 * b) : 0);

                const RgbColor cube_color{red, green, blue};
                const auto dist = perceptual_distance(target, cube_color);
                if (dist < best_distance)
                {
                    best_distance = dist;
                    best_index = index;
                }
            }
        }
    }

    // Also check the 16 standard ANSI colors
    constexpr RgbColor ansi_colors[16] = {
        {0, 0, 0},       // Black
        {128, 0, 0},     // Red
        {0, 128, 0},     // Green
        {128, 128, 0},   // Yellow
        {0, 0, 128},     // Blue
        {128, 0, 128},   // Magenta
        {0, 128, 128},   // Cyan
        {192, 192, 192}, // White
        {128, 128, 128}, // Bright Black
        {255, 0, 0},     // Bright Red
        {0, 255, 0},     // Bright Green
        {255, 255, 0},   // Bright Yellow
        {0, 0, 255},     // Bright Blue
        {255, 0, 255},   // Bright Magenta
        {0, 255, 255},   // Bright Cyan
        {255, 255, 255}, // Bright White
    };

    for (std::uint8_t i = 0; i < 16; ++i)
    {
        const auto dist = perceptual_distance(target, ansi_colors[i]);
        if (dist < best_distance)
        {
            best_distance = dist;
            best_index = i;
        }
    }

    return best_index;
}

/// Calculates the background color for user messages.
/// On light backgrounds: subtle dark overlay (alpha = 0.04)
/// On dark backgrounds: subtle light overlay (alpha = 0.12)
[[nodiscard]] inline auto user_message_bg(RgbColor terminal_bg) noexcept -> RgbColor
{
    if (is_light(terminal_bg))
    {
        // Light background: add subtle dark tint
        return blend(RgbColor{0, 0, 0}, terminal_bg, 0.04F);
    }
    // Dark background: add subtle light tint
    return blend(RgbColor{255, 255, 255}, terminal_bg, 0.12F);
}

/// Calculates the background color for proposed plan cells.
/// Currently identical to user_message_bg for visual consistency.
[[nodiscard]] inline auto proposed_plan_bg(RgbColor terminal_bg) noexcept -> RgbColor
{
    return user_message_bg(terminal_bg);
}

/// Accent color for light backgrounds (darker cyan for contrast).
constexpr RgbColor k_light_bg_accent{0, 95, 135};

/// Accent color for dark backgrounds (standard cyan).
constexpr RgbColor k_dark_bg_accent{0, 255, 255};

/// Gets the appropriate accent color based on terminal background.
[[nodiscard]] inline auto accent_color_for_bg(RgbColor terminal_bg) noexcept -> RgbColor
{
    if (is_light(terminal_bg))
    {
        return k_light_bg_accent;
    }
    return k_dark_bg_accent;
}

} // namespace codeharness::tui
