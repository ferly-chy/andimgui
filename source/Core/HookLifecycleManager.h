#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

class HookLifecycleManager {
public:
    struct Entry {
        std::string name;
        void* target = nullptr;
        void* replacement = nullptr;
        void** original = nullptr;
        int result = -1;
        bool installed = false;
    };

    static HookLifecycleManager& GetInstance();

    HookLifecycleManager(const HookLifecycleManager&) = delete;
    HookLifecycleManager& operator=(const HookLifecycleManager&) = delete;

    [[nodiscard]] bool install(std::string_view name, void* target, void* replacement, void** original);
    void uninstallAll() noexcept;
    void clear() noexcept;

    [[nodiscard]] std::vector<Entry> snapshot() const;
    [[nodiscard]] std::size_t installedCount() const noexcept;
    [[nodiscard]] std::size_t totalCount() const noexcept;

private:
    HookLifecycleManager() = default;
    ~HookLifecycleManager() = default;
};
