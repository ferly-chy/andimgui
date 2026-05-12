#pragma once

#include "imgui/imgui.h"
#include <atomic>

namespace UIState {
    extern ImTextureID g_IconTextureID;
    extern ImVec2 g_IconPosition;
    extern std::atomic<bool> g_IsMenuVisible;
}
