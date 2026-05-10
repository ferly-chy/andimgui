#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class HookLifecycleManager {
public:
    enum class Backend : std::uint8_t {
        Dobby,
    };

    enum class State : std::uint8_t {
        Pending,
        Installed,
        Failed,
        InvalidRequest,
        Duplicate,
        Uninstalled,
        DestroyFailed,
    };

    struct Entry {
        std::string name;
        std::string owner;
        Backend backend = Backend::Dobby;
        State state = State::Pending;
        void* target = nullptr;
        void* replacement = nullptr;
        void** original = nullptr;
        int installResult = -1;
        int uninstallResult = -1;
        std::uint64_t sequence = 0;
    };

    static HookLifecycleManager& GetInstance();

    HookLifecycleManager(const HookLifecycleManager&) = delete;
    HookLifecycleManager& operator=(const HookLifecycleManager&) = delete;

    [[nodiscard]] bool install(std::string_view name,
                               void* target,
                               void* replacement,
                               void** original,
                               std::string_view owner = "General");

    void uninstallByOwner(std::string_view owner) noexcept;
    void clearByOwner(std::string_view owner) noexcept;
    void uninstallAll() noexcept;
    void clear() noexcept;

    [[nodiscard]] std::vector<Entry> snapshot() const;
    [[nodiscard]] std::size_t installedCount() const noexcept;
    [[nodiscard]] std::size_t totalCount() const noexcept;

    [[nodiscard]] static std::string_view backendName(Backend backend) noexcept;
    [[nodiscard]] static std::string_view stateName(State state) noexcept;

private:
    HookLifecycleManager() = default;
    ~HookLifecycleManager() = default;
};
