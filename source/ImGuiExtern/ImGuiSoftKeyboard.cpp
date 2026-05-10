#include "ImGuiSoftKeyboard.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <array>
#include <cstdint>
#include <span>

namespace ImGuiSoftKeyboard
{
namespace {

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------
bool g_ForceShow  = false;
bool g_ForceState = false;
bool g_Visible    = false;
bool g_Shift      = false;
bool g_Caps       = false;
bool g_Symbols    = false;   // 符号层
int  g_PressedKey = -1;      // 当前帧被按下的按键索引 (用于高亮)

// 记录本帧是否有点击落在键盘区域（PreUpdate 设置，Draw 读取）
bool  g_ClickInKeyboard = false;
ImVec2 g_ClickMousePos   = ImVec2(0, 0);

// ---------------------------------------------------------------------------
// 键盘布局定义
// ---------------------------------------------------------------------------

enum class KeyAction : std::uint8_t {
    Char,
    Backspace,
    Enter,
    Shift,
    Space,
    Symbols,
    Tab,
    Left,
    Right,
    Hide,
};

struct KeyDef {
    const char* label;       // 显示文本
    const char* shiftLabel;  // Shift 时显示
    float       widthScale;  // 相对于普通键的宽度倍数
    KeyAction   action;
};

using KeyRow = std::span<const KeyDef>;

constexpr std::array<KeyDef, 10> kAlphaRow0 = {{
    {"1", "!", 1.0f, KeyAction::Char}, {"2", "@", 1.0f, KeyAction::Char}, {"3", "#", 1.0f, KeyAction::Char}, {"4", "$", 1.0f, KeyAction::Char}, {"5", "%", 1.0f, KeyAction::Char},
    {"6", "^", 1.0f, KeyAction::Char}, {"7", "&", 1.0f, KeyAction::Char}, {"8", "*", 1.0f, KeyAction::Char}, {"9", "(", 1.0f, KeyAction::Char}, {"0", ")", 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 10> kAlphaRow1 = {{
    {"q", "Q", 1.0f, KeyAction::Char}, {"w", "W", 1.0f, KeyAction::Char}, {"e", "E", 1.0f, KeyAction::Char}, {"r", "R", 1.0f, KeyAction::Char}, {"t", "T", 1.0f, KeyAction::Char},
    {"y", "Y", 1.0f, KeyAction::Char}, {"u", "U", 1.0f, KeyAction::Char}, {"i", "I", 1.0f, KeyAction::Char}, {"o", "O", 1.0f, KeyAction::Char}, {"p", "P", 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 9> kAlphaRow2 = {{
    {"a", "A", 1.0f, KeyAction::Char}, {"s", "S", 1.0f, KeyAction::Char}, {"d", "D", 1.0f, KeyAction::Char}, {"f", "F", 1.0f, KeyAction::Char}, {"g", "G", 1.0f, KeyAction::Char},
    {"h", "H", 1.0f, KeyAction::Char}, {"j", "J", 1.0f, KeyAction::Char}, {"k", "K", 1.0f, KeyAction::Char}, {"l", "L", 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 9> kAlphaRow3 = {{
    {"Shift", nullptr, 1.5f, KeyAction::Shift}, {"z", "Z", 1.0f, KeyAction::Char}, {"x", "X", 1.0f, KeyAction::Char}, {"c", "C", 1.0f, KeyAction::Char}, {"v", "V", 1.0f, KeyAction::Char},
    {"b", "B", 1.0f, KeyAction::Char}, {"n", "N", 1.0f, KeyAction::Char}, {"m", "M", 1.0f, KeyAction::Char}, {"<-", nullptr, 1.5f, KeyAction::Backspace},
}};
constexpr std::array<KeyDef, 5> kAlphaRow4 = {{
    {"?!#", nullptr, 1.5f, KeyAction::Symbols}, {",", nullptr, 1.0f, KeyAction::Char}, {" ", nullptr, 5.0f, KeyAction::Space}, {".", nullptr, 1.0f, KeyAction::Char}, {"OK", nullptr, 1.5f, KeyAction::Enter},
}};

constexpr std::array<KeyDef, 10> kSymbolRow0 = {{
    {"~", nullptr, 1.0f, KeyAction::Char}, {"`", nullptr, 1.0f, KeyAction::Char}, {"|", nullptr, 1.0f, KeyAction::Char}, {"\\", nullptr, 1.0f, KeyAction::Char},
    {"{", nullptr, 1.0f, KeyAction::Char}, {"}", nullptr, 1.0f, KeyAction::Char}, {"[", nullptr, 1.0f, KeyAction::Char}, {"]", nullptr, 1.0f, KeyAction::Char},
    {"<", nullptr, 1.0f, KeyAction::Char}, {">", nullptr, 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 10> kSymbolRow1 = {{
    {"!", nullptr, 1.0f, KeyAction::Char}, {"@", nullptr, 1.0f, KeyAction::Char}, {"#", nullptr, 1.0f, KeyAction::Char}, {"$", nullptr, 1.0f, KeyAction::Char},
    {"%", nullptr, 1.0f, KeyAction::Char}, {"^", nullptr, 1.0f, KeyAction::Char}, {"&", nullptr, 1.0f, KeyAction::Char}, {"*", nullptr, 1.0f, KeyAction::Char},
    {"(", nullptr, 1.0f, KeyAction::Char}, {")", nullptr, 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 9> kSymbolRow2 = {{
    {"-", nullptr, 1.0f, KeyAction::Char}, {"_", nullptr, 1.0f, KeyAction::Char}, {"=", nullptr, 1.0f, KeyAction::Char}, {"+", nullptr, 1.0f, KeyAction::Char},
    {";", nullptr, 1.0f, KeyAction::Char}, {":", nullptr, 1.0f, KeyAction::Char}, {"'", nullptr, 1.0f, KeyAction::Char}, {"\"", nullptr, 1.0f, KeyAction::Char}, {"/", nullptr, 1.0f, KeyAction::Char},
}};
constexpr std::array<KeyDef, 5> kSymbolRow3 = {{
    {"Tab", nullptr, 1.5f, KeyAction::Tab}, {"?", nullptr, 1.0f, KeyAction::Char}, {"<", nullptr, 1.0f, KeyAction::Left}, {">", nullptr, 1.0f, KeyAction::Right}, {"<-", nullptr, 1.5f, KeyAction::Backspace},
}};
constexpr std::array<KeyDef, 5> kSymbolRow4 = {{
    {"ABC", nullptr, 1.5f, KeyAction::Symbols}, {",", nullptr, 1.0f, KeyAction::Char}, {" ", nullptr, 5.0f, KeyAction::Space}, {".", nullptr, 1.0f, KeyAction::Char}, {"OK", nullptr, 1.5f, KeyAction::Enter},
}};

const std::array<KeyRow, 5> kAlphaRows = {
    KeyRow{kAlphaRow0}, KeyRow{kAlphaRow1}, KeyRow{kAlphaRow2}, KeyRow{kAlphaRow3}, KeyRow{kAlphaRow4},
};
const std::array<KeyRow, 5> kSymbolRows = {
    KeyRow{kSymbolRow0}, KeyRow{kSymbolRow1}, KeyRow{kSymbolRow2}, KeyRow{kSymbolRow3}, KeyRow{kSymbolRow4},
};

static_assert(kAlphaRows.size() == kSymbolRows.size());

[[nodiscard]] constexpr std::size_t RowCount() noexcept {
    return kAlphaRows.size();
}

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

[[nodiscard]] bool IsUpper() noexcept
{
    return g_Shift ^ g_Caps;
}

[[nodiscard]] const char* DisplayTextFor(const KeyDef& key) noexcept
{
    return key.action == KeyAction::Char && IsUpper() && key.shiftLabel ? key.shiftLabel : key.label;
}

void AddKeyPress(ImGuiIO& io, ImGuiKey key) noexcept
{
    io.AddKeyEvent(key, true);
    io.AddKeyEvent(key, false);
}

void AddUtf8Text(ImGuiIO& io, const char* text)
{
    if (!text || !text[0]) {
        return;
    }

    const char* p = text;
    while (*p)
    {
        unsigned int c = 0;
        int adv = ImTextCharFromUtf8(&c, p, nullptr);
        if (adv <= 0) break;
        io.AddInputCharacter(c);
        p += adv;
    }
}

void ProcessKey(const KeyDef& key)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (key.action)
    {
    case KeyAction::Char:
        AddUtf8Text(io, DisplayTextFor(key));
        if (g_Shift && !g_Caps)
            g_Shift = false;
        break;
    case KeyAction::Backspace:
        AddKeyPress(io, ImGuiKey_Backspace);
        break;
    case KeyAction::Enter:
        AddKeyPress(io, ImGuiKey_Enter);
        break;
    case KeyAction::Tab:
        AddKeyPress(io, ImGuiKey_Tab);
        break;
    case KeyAction::Left:
        AddKeyPress(io, ImGuiKey_LeftArrow);
        break;
    case KeyAction::Right:
        AddKeyPress(io, ImGuiKey_RightArrow);
        break;
    case KeyAction::Shift:
        g_Shift = !g_Shift;
        break;
    case KeyAction::Symbols:
        g_Symbols = !g_Symbols;
        break;
    case KeyAction::Space:
        io.AddInputCharacter(' ');
        break;
    case KeyAction::Hide:
        break;
    }
}

/// 检测屏幕坐标是否在矩形内
[[nodiscard]] bool HitTest(const ImVec2& pos, const ImVec2& rectMin, const ImVec2& rectMax) noexcept
{
    return pos.x >= rectMin.x && pos.x < rectMax.x &&
           pos.y >= rectMin.y && pos.y < rectMax.y;
}

struct KeyboardRect {
    float y = 0.0f;
    float height = 0.0f;
};

// ---------------------------------------------------------------------------
// 键盘区域计算（复用）
// ---------------------------------------------------------------------------
[[nodiscard]] KeyboardRect GetKeyboardRect(const ImGuiIO& io) noexcept
{
    const float height = io.DisplaySize.y * 0.38f;
    return KeyboardRect{
        .y = io.DisplaySize.y - height,
        .height = height,
    };
}

[[nodiscard]] bool IsInKeyboardArea(const ImGuiIO& io, const ImVec2& pos) noexcept
{
    const auto rect = GetKeyboardRect(io);
    return HitTest(pos, ImVec2(0.0f, rect.y), ImVec2(io.DisplaySize.x, io.DisplaySize.y));
}

[[nodiscard]] float RowWidthScale(KeyRow row) noexcept
{
    float total = 0.0f;
    for (const auto& key : row) {
        total += key.widthScale;
    }
    return total;
}

void DrawKey(ImDrawList& drawList,
             const KeyDef& key,
             const ImVec2& btnMin,
             const ImVec2& btnMax,
             float btnW,
             float rowHeight,
             bool isClicked)
{
    const bool isSpecial = key.action != KeyAction::Char && key.action != KeyAction::Space;
    const bool isShiftActive = key.action == KeyAction::Shift && (g_Shift || g_Caps);

    const ImU32 btnColor = isClicked ? IM_COL32(100, 130, 200, 255)
                         : isShiftActive ? IM_COL32(77, 128, 204, 255)
                         : isSpecial ? IM_COL32(64, 64, 77, 255)
                                     : IM_COL32(55, 55, 60, 255);

    drawList.AddRectFilled(btnMin, btnMax, btnColor, 6.0f);

    const char* displayText = DisplayTextFor(key);
    if (displayText && displayText[0])
    {
        const ImVec2 textSize = ImGui::CalcTextSize(displayText);
        const ImVec2 textPos(
            btnMin.x + (btnW - textSize.x) * 0.5f,
            btnMin.y + (rowHeight - textSize.y) * 0.5f
        );
        drawList.AddText(textPos, IM_COL32(255, 255, 255, 255), displayText);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------

void PreUpdate()
{
    ImGuiIO& io = ImGui::GetIO();

    // ── 可见性锁存 ──────────────────────────────────────────────
    if (g_ForceState)
    {
        g_Visible = g_ForceShow;
    }
    else
    {
        if (io.WantTextInput && !g_Visible)
        {
            g_Visible = true;
        }
        else if (g_Visible && !io.WantTextInput)
        {
            // 仅当用户点击了键盘区域之外时才关闭
            if (io.MouseClicked[0] && !IsInKeyboardArea(io, io.MousePos))
            {
                g_Visible = false;
            }
        }
    }

    g_ClickInKeyboard = false;
    g_ClickMousePos = io.MousePos;

    if (!g_Visible)
        return;

    // ── 在用户 UI 之前拦截键盘区域的鼠标事件 ─────────────────────
    const bool inKbArea = IsInKeyboardArea(io, io.MousePos);

    if (io.MouseClicked[0] && inKbArea)
    {
        g_ClickInKeyboard = true;
        io.MouseClicked[0] = false;
    }
    if (io.MouseDown[0] && inKbArea)
    {
        io.MouseDown[0] = false;
    }
}

void Draw()
{
    if (!g_Visible)
        return;

    ImGuiIO& io = ImGui::GetIO();

    // ── 键盘布局计算 ────────────────────────────────────────────
    const float screenW = io.DisplaySize.x;
    const float screenH = io.DisplaySize.y;
    const auto keyboardRect = GetKeyboardRect(io);
    constexpr float padding = 4.0f;
    constexpr float keySpacing = 4.0f;

    const auto& rows = g_Symbols ? kSymbolRows : kAlphaRows;
    const float rowHeight = (keyboardRect.height - padding * 2.0f - keySpacing * static_cast<float>(RowCount() - 1)) /
                            static_cast<float>(RowCount());

    // 使用 PreUpdate 中记录的点击状态
    const bool clickInKeyboard = g_ClickInKeyboard;
    const ImVec2 mousePos = g_ClickMousePos;

    // ── 使用前景 DrawList 绘制（不创建 ImGui 窗口，不抢焦点）─────
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    // 背景
    drawList->AddRectFilled(ImVec2(0, keyboardRect.y), ImVec2(screenW, screenH), IM_COL32(38, 38, 38, 242));

    g_PressedKey = -1;
    int globalKeyIdx = 0;

    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        const KeyRow row = rows[r];
        const float totalScale = RowWidthScale(row);
        const float availW = screenW - padding * 2.0f - keySpacing * static_cast<float>(row.size() - 1);
        const float unitW = availW / totalScale;

        float curX = padding;
        const float curY = keyboardRect.y + padding + static_cast<float>(r) * (rowHeight + keySpacing);

        for (const auto& key : row)
        {
            const float btnW = unitW * key.widthScale;

            const ImVec2 btnMin(curX, curY);
            const ImVec2 btnMax(curX + btnW, curY + rowHeight);

            // 点击检测
            const bool isClicked = clickInKeyboard && HitTest(mousePos, btnMin, btnMax);

            DrawKey(*drawList, key, btnMin, btnMax, btnW, rowHeight, isClicked);

            // 处理点击
            if (isClicked)
            {
                g_PressedKey = globalKeyIdx;

                if (key.action == KeyAction::Enter)
                {
                    // Enter/OK: 关闭键盘
                    g_Visible = false;
                }

                ProcessKey(key);
            }

            curX += btnW + keySpacing;
            ++globalKeyIdx;
        }
    }

}

void ForceShow(bool show)
{
    g_ForceState = true;
    g_ForceShow = show;
}

bool IsVisible()
{
    return g_Visible;
}

} // namespace ImGuiSoftKeyboard
