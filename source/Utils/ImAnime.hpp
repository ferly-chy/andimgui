#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <string>

namespace ImGuiFX {

/**
 * =========================
 * CATPPUCCIN FRAPPÉ + PASTEL PALETTE
 * =========================
 */
namespace Colors {

constexpr ImVec4 Rosewater = ImVec4(0.95f, 0.84f, 0.81f, 1.00f);
constexpr ImVec4 Flamingo  = ImVec4(0.93f, 0.75f, 0.75f, 1.00f);
constexpr ImVec4 Pink      = ImVec4(0.96f, 0.72f, 0.89f, 1.00f);
constexpr ImVec4 Mauve     = ImVec4(0.79f, 0.62f, 0.90f, 1.00f);
constexpr ImVec4 Red       = ImVec4(0.91f, 0.51f, 0.52f, 1.00f);
constexpr ImVec4 Peach     = ImVec4(0.94f, 0.62f, 0.46f, 1.00f);
constexpr ImVec4 Yellow    = ImVec4(0.90f, 0.78f, 0.56f, 1.00f);
constexpr ImVec4 Green     = ImVec4(0.65f, 0.82f, 0.54f, 1.00f);
constexpr ImVec4 Teal      = ImVec4(0.51f, 0.78f, 0.75f, 1.00f);
constexpr ImVec4 Sky       = ImVec4(0.60f, 0.82f, 0.86f, 1.00f);
constexpr ImVec4 Sapphire  = ImVec4(0.52f, 0.76f, 0.86f, 1.00f);
constexpr ImVec4 Blue      = ImVec4(0.55f, 0.67f, 0.93f, 1.00f);
constexpr ImVec4 Lavender  = ImVec4(0.73f, 0.73f, 0.95f, 1.00f);

constexpr ImVec4 BlushPink     = ImVec4(0.98f, 0.82f, 0.82f, 1.00f);
constexpr ImVec4 Bubblegum     = ImVec4(1.00f, 0.76f, 0.80f, 1.00f);
constexpr ImVec4 PinkPastel    = ImVec4(1.00f, 0.72f, 0.77f, 1.00f);
constexpr ImVec4 RainbowCyan   = ImVec4(0.88f, 1.00f, 1.00f, 1.00f);
constexpr ImVec4 RainbowYellow = ImVec4(1.00f, 0.98f, 0.77f, 1.00f);
constexpr ImVec4 RainbowGreen  = ImVec4(0.91f, 0.96f, 0.91f, 1.00f);

inline ImU32 ToU32(ImVec4 color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

} // namespace Colors

/**
 * @brief Animated Catppuccin/Pastel gradient
 */
inline ImVec4 GetGradient(float offset) {
    constexpr ImVec4 palette[] = {
        Colors::Mauve,
        Colors::Pink,
        Colors::Rosewater,
        Colors::Peach,
        Colors::Yellow,
        Colors::Green,
        Colors::Teal,
        Colors::Sky,
        Colors::Blue,
        Colors::Lavender,
        Colors::Bubblegum,
        Colors::RainbowCyan
    };

    constexpr int count = sizeof(palette) / sizeof(palette[0]);

    float t = fmodf(ImGui::GetTime() * 1.5f + offset, (float)count);
    int i = (int)t;
    float f = t - i;

    const ImVec4& c1 = palette[i % count];
    const ImVec4& c2 = palette[(i + 1) % count];

    return ImVec4(
        c1.x + (c2.x - c1.x) * f,
        c1.y + (c2.y - c1.y) * f,
        c1.z + (c2.z - c1.z) * f,
        1.0f
    );
}

/**
 * @brief Draw rotating star
 */
inline void DrawRotatingStar(
    ImDrawList* draw_list,
    ImVec2 center,
    float radius,
    ImU32 color,
    float angle
) {
    const int points = 5;
    ImVec2 pts[points * 2];

    for (int i = 0; i < points * 2; i++) {
        float r = (i & 1) ? radius * 0.45f : radius;
        float a = angle + i * IM_PI / points;

        pts[i] = ImVec2(
            center.x + cosf(a) * r,
            center.y + sinf(a) * r
        );
    }

    draw_list->AddPolyline(
        pts,
        points * 2,
        color,
        ImDrawFlags_Closed,
        2.2f
    );
}

/**
 * @brief Animated rainbow/catppuccin text
 */
inline void TextRainbow(
    const std::string& text,
    float speed_mult = 1.0f
) {
    for (size_t i = 0; i < text.length(); ++i) {
        ImGui::TextColored(
            GetGradient(i * 0.15f * speed_mult),
            "%c",
            text[i]
        );

        if (i < text.length() - 1)
            ImGui::SameLine(0, 0);
    }
}

} // namespace ImGuiFX
