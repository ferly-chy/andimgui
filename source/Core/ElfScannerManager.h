#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>

// ============================================================
//  X-macro library registry — add new target libraries here.
//  Format: ELF_LIB_ENTRY(enum, accessor, "full_so_name")
// ============================================================
#define ELF_LIB_LIST                                                    \
    ELF_LIB_ENTRY(C,               c,               "libc.so")               \
    ELF_LIB_ENTRY(CRYPTO,          crypto,          "libcrypto.so")          \
    ELF_LIB_ENTRY(UE4,             UE4,             "libUE4.so")             \
    ELF_LIB_ENTRY(UNITY,           unity,           "libunity.so")           \
    ELF_LIB_ENTRY(IL2CPP,          il2cpp,          "libil2cpp.so")          \
    ELF_LIB_ENTRY(TERSAFE,         tersafe,         "libtersafe.so")         \
    ELF_LIB_ENTRY(GAME,            game,            "libgame.so")            \
    ELF_LIB_ENTRY(VULKAN,          vulkan,          "libvulkan.so")          \
    ELF_LIB_ENTRY(INPUT,           input,           "libinput.so")           \
    ELF_LIB_ENTRY(ART,             art,             "libart.so")             \
    ELF_LIB_ENTRY(ANDROID_RUNTIME, android_runtime, "libandroid_runtime.so") \
    ELF_LIB_ENTRY(GODOT,           godot,           "libgodot_android.so")   \
    ELF_LIB_ENTRY(SEC2026,         sec2026,         "libsec2026.so")         \
    ELF_LIB_ENTRY(EGL,             egl,             "libEGL.so")

class XdlLibrary {
public:
    XdlLibrary() = default;
    ~XdlLibrary();

    XdlLibrary(const XdlLibrary&) = delete;
    XdlLibrary& operator=(const XdlLibrary&) = delete;

    XdlLibrary(XdlLibrary&& other) noexcept;
    XdlLibrary& operator=(XdlLibrary&& other) noexcept;

    bool open(std::string libraryName);
    bool isValid() const { return m_handle != nullptr; }
    void* findSymbol(const char* symbol, size_t* symbolSize = nullptr) const;
    uintptr_t base() const { return m_base; }
    const std::string& name() const { return m_name; }
    void close();

private:
    std::string m_name;
    void* m_handle = nullptr;
    uintptr_t m_base = 0;
};

class ElfScannerManager {
public:
    enum Lib : int {
#define ELF_LIB_ENTRY(ENUM, FUNC, SO) LIB_##ENUM,
        ELF_LIB_LIST
#undef ELF_LIB_ENTRY
        LIB_COUNT
    };

    static ElfScannerManager& GetInstance() {
        static ElfScannerManager g_Instance;
        return g_Instance;
    }

    ElfScannerManager(const ElfScannerManager&) = delete;
    ElfScannerManager& operator=(const ElfScannerManager&) = delete;

    bool scanAsync(const std::set<std::string>& libraries);

#define ELF_LIB_ENTRY(ENUM, FUNC, SO) \
    XdlLibrary& FUNC() { return m_libraries[LIB_##ENUM]; }
    ELF_LIB_LIST
#undef ELF_LIB_ENTRY

private:
    ElfScannerManager() = default;
    ~ElfScannerManager() = default;

    static int libNameToIndex(std::string_view libraryName);

    std::array<XdlLibrary, LIB_COUNT> m_libraries{};
};

inline ElfScannerManager& Elf = ElfScannerManager::GetInstance();
