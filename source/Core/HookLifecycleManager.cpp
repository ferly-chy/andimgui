#include "HookLifecycleManager.h"

#include "Dobby/dobby.h"
#include "Utils/Logger.h"

#include <algorithm>
#include <mutex>

namespace {

std::mutex g_HookMutex;
std::vector<HookLifecycleManager::Entry> g_Hooks;

} // namespace

HookLifecycleManager& HookLifecycleManager::GetInstance() {
    static HookLifecycleManager instance;
    return instance;
}

bool HookLifecycleManager::install(std::string_view name, void* target, void* replacement, void** original) {
    std::lock_guard lock{g_HookMutex};

    Entry entry{
        .name = std::string{name},
        .target = target,
        .replacement = replacement,
        .original = original,
        .result = -1,
        .installed = false,
    };

    if (!target || !replacement || !original) {
        LOGE("[HookLifecycle] Invalid hook request: %.*s target=%p replacement=%p original=%p",
             static_cast<int>(name.size()), name.data(), target, replacement, original);
        g_Hooks.push_back(entry);
        return false;
    }

    entry.result = DobbyHook(target, replacement, original);
    entry.installed = (entry.result == 0);

    LOGI("[HookLifecycle] DobbyHook(%.*s)=%d target=%p orig=%p",
         static_cast<int>(name.size()), name.data(), entry.result, target, *original);

    g_Hooks.push_back(entry);
    return entry.installed;
}

void HookLifecycleManager::uninstallAll() noexcept {
    std::lock_guard lock{g_HookMutex};

    for (auto it = g_Hooks.rbegin(); it != g_Hooks.rend(); ++it) {
        if (!it->installed || !it->target) {
            continue;
        }

        const int result = DobbyDestroy(it->target);
        LOGI("[HookLifecycle] DobbyDestroy(%.*s)=%d target=%p",
             static_cast<int>(it->name.size()), it->name.data(), result, it->target);
        it->installed = false;
    }
}

void HookLifecycleManager::clear() noexcept {
    std::lock_guard lock{g_HookMutex};
    g_Hooks.clear();
}

std::vector<HookLifecycleManager::Entry> HookLifecycleManager::snapshot() const {
    std::lock_guard lock{g_HookMutex};
    return g_Hooks;
}

std::size_t HookLifecycleManager::installedCount() const noexcept {
    std::lock_guard lock{g_HookMutex};
    return static_cast<std::size_t>(std::ranges::count_if(g_Hooks, [](const Entry& entry) {
        return entry.installed;
    }));
}

std::size_t HookLifecycleManager::totalCount() const noexcept {
    std::lock_guard lock{g_HookMutex};
    return g_Hooks.size();
}
