#include "HookLifecycleManager.h"

#include "Dobby/dobby.h"
#include "Utils/Logger.h"

#include <algorithm>
#include <mutex>
#include <string>

namespace {

std::mutex g_HookMutex;
std::vector<HookLifecycleManager::Entry> g_Hooks;
std::uint64_t g_NextSequence = 1;

[[nodiscard]] bool IsInstalled(const HookLifecycleManager::Entry& entry) noexcept {
    return entry.state == HookLifecycleManager::State::Installed;
}

[[nodiscard]] HookLifecycleManager::Entry MakeEntry(std::string_view name,
                                                    void* target,
                                                    void* replacement,
                                                    void** original,
                                                    std::string_view owner) {
    return HookLifecycleManager::Entry{
        .name = std::string{name},
        .owner = std::string{owner},
        .backend = HookLifecycleManager::Backend::Dobby,
        .state = HookLifecycleManager::State::Pending,
        .target = target,
        .replacement = replacement,
        .original = original,
        .installResult = -1,
        .uninstallResult = -1,
        .sequence = g_NextSequence++,
    };
}

} // namespace

HookLifecycleManager& HookLifecycleManager::GetInstance() {
    static HookLifecycleManager instance;
    return instance;
}

bool HookLifecycleManager::install(std::string_view name,
                                   void* target,
                                   void* replacement,
                                   void** original,
                                   std::string_view owner) {
    std::lock_guard lock{g_HookMutex};

    auto entry = MakeEntry(name, target, replacement, original, owner);

    if (!target || !replacement || !original) {
        entry.state = State::InvalidRequest;
        LOGE("[HookLifecycle] Invalid Dobby hook request: owner=%.*s name=%.*s target=%p replacement=%p original=%p",
             static_cast<int>(owner.size()), owner.data(),
             static_cast<int>(name.size()), name.data(), target, replacement, original);
        g_Hooks.push_back(std::move(entry));
        return false;
    }

    const auto duplicate = std::ranges::find_if(g_Hooks, [target](const Entry& existing) {
        return existing.target == target && IsInstalled(existing);
    });
    if (duplicate != g_Hooks.end()) {
        entry.state = State::Duplicate;
        entry.installResult = duplicate->installResult;
        bool copiedOriginal = false;
        if (original && duplicate->original && *duplicate->original) {
            *original = *duplicate->original;
            copiedOriginal = true;
        }
        LOGW("[HookLifecycle] Duplicate Dobby hook skipped: owner=%.*s name=%.*s target=%p existing=%s/%s copiedOriginal=%s",
             static_cast<int>(owner.size()), owner.data(),
             static_cast<int>(name.size()), name.data(), target,
             duplicate->owner.c_str(), duplicate->name.c_str(), copiedOriginal ? "yes" : "no");
        g_Hooks.push_back(std::move(entry));
        return copiedOriginal;
    }

    entry.installResult = DobbyHook(target, replacement, original);
    entry.state = (entry.installResult == 0 && *original) ? State::Installed : State::Failed;

    LOGI("[HookLifecycle] DobbyHook owner=%.*s name=%.*s result=%d target=%p orig=%p state=%.*s seq=%llu",
         static_cast<int>(owner.size()), owner.data(),
         static_cast<int>(name.size()), name.data(), entry.installResult, target, *original,
         static_cast<int>(stateName(entry.state).size()), stateName(entry.state).data(),
         static_cast<unsigned long long>(entry.sequence));

    g_Hooks.push_back(std::move(entry));
    return g_Hooks.back().state == State::Installed;
}

void HookLifecycleManager::uninstallByOwner(std::string_view owner) noexcept {
    std::lock_guard lock{g_HookMutex};

    for (auto it = g_Hooks.rbegin(); it != g_Hooks.rend(); ++it) {
        if (!IsInstalled(*it) || it->owner != owner || !it->target) {
            continue;
        }

        it->uninstallResult = DobbyDestroy(it->target);
        it->state = (it->uninstallResult == 0) ? State::Uninstalled : State::DestroyFailed;
        LOGI("[HookLifecycle] DobbyDestroy owner=%s name=%s result=%d target=%p state=%.*s seq=%llu",
             it->owner.c_str(), it->name.c_str(), it->uninstallResult, it->target,
             static_cast<int>(stateName(it->state).size()), stateName(it->state).data(),
             static_cast<unsigned long long>(it->sequence));
    }
}

void HookLifecycleManager::uninstallAll() noexcept {
    std::lock_guard lock{g_HookMutex};

    for (auto it = g_Hooks.rbegin(); it != g_Hooks.rend(); ++it) {
        if (!IsInstalled(*it) || !it->target) {
            continue;
        }

        it->uninstallResult = DobbyDestroy(it->target);
        it->state = (it->uninstallResult == 0) ? State::Uninstalled : State::DestroyFailed;
        LOGI("[HookLifecycle] DobbyDestroy owner=%s name=%s result=%d target=%p state=%.*s seq=%llu",
             it->owner.c_str(), it->name.c_str(), it->uninstallResult, it->target,
             static_cast<int>(stateName(it->state).size()), stateName(it->state).data(),
             static_cast<unsigned long long>(it->sequence));
    }
}

void HookLifecycleManager::clearByOwner(std::string_view owner) noexcept {
    std::lock_guard lock{g_HookMutex};
    std::erase_if(g_Hooks, [owner](const Entry& entry) {
        return entry.owner == owner;
    });
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
    return static_cast<std::size_t>(std::ranges::count_if(g_Hooks, IsInstalled));
}

std::size_t HookLifecycleManager::totalCount() const noexcept {
    std::lock_guard lock{g_HookMutex};
    return g_Hooks.size();
}

std::string_view HookLifecycleManager::backendName(Backend backend) noexcept {
    switch (backend) {
    case Backend::Dobby:
        return "Dobby";
    }
    return "Unknown";
}

std::string_view HookLifecycleManager::stateName(State state) noexcept {
    switch (state) {
    case State::Pending:
        return "pending";
    case State::Installed:
        return "installed";
    case State::Failed:
        return "failed";
    case State::InvalidRequest:
        return "invalid";
    case State::Duplicate:
        return "duplicate";
    case State::Uninstalled:
        return "uninstalled";
    case State::DestroyFailed:
        return "destroy-failed";
    }
    return "unknown";
}
