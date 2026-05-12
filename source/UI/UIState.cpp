#include "UIState.h"

namespace UIState {
    ImTextureID g_IconTextureID = 0;
    ImVec2 g_IconPosition = ImVec2(100, 100);
    std::atomic<bool> g_IsMenuVisible(false);
}
