#include "ElfScannerManager.h"

#include <chrono>
#include <future>
#include <thread>
#include <utility>
#include <vector>

#include "Utils/Logger.h"
#include "xdl.h"

namespace {

static constexpr struct { std::string_view soName; int index; } kLibTable[] = {
#define ELF_LIB_ENTRY(ENUM, FUNC, SO) { SO, ElfScannerManager::LIB_##ENUM },
    ELF_LIB_LIST
#undef ELF_LIB_ENTRY
};

} // namespace

XdlLibrary::~XdlLibrary() { close(); }

XdlLibrary::XdlLibrary(XdlLibrary&& other) noexcept
    : m_name(std::move(other.m_name))
    , m_handle(other.m_handle)
    , m_base(other.m_base)
{
    other.m_handle = nullptr;
    other.m_base = 0;
}

XdlLibrary& XdlLibrary::operator=(XdlLibrary&& other) noexcept {
    if (this != &other) {
        close();
        m_name = std::move(other.m_name);
        m_handle = other.m_handle;
        m_base = other.m_base;
        other.m_handle = nullptr;
        other.m_base = 0;
    }
    return *this;
}

bool XdlLibrary::open(std::string libraryName) {
    close();
    m_name = std::move(libraryName);

    for (int i = 0; i < 500 && !m_handle; ++i) {
        m_handle = xdl_open(m_name.c_str(), XDL_DEFAULT);
        if (!m_handle)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!m_handle) {
        LOGE("[XdlLibrary] xdl_open failed: %s", m_name.c_str());
        return false;
    }

    xdl_info_t info{};
    if (xdl_info(m_handle, XDL_DI_DLINFO, &info) == 0 && info.dli_fbase) {
        m_base = reinterpret_cast<uintptr_t>(info.dli_fbase);
    }

    LOGI("[XdlLibrary] opened %s handle=%p base=0x%lX", m_name.c_str(),
         m_handle, static_cast<unsigned long>(m_base));
    return true;
}

void* XdlLibrary::findSymbol(const char* symbol, size_t* symbolSize) const {
    if (!m_handle || !symbol)
        return nullptr;

    size_t localSize = 0;
    void* addr = xdl_sym(m_handle, symbol, symbolSize ? symbolSize : &localSize);
    if (!addr)
        addr = xdl_dsym(m_handle, symbol, symbolSize ? symbolSize : &localSize);
    return addr;
}

void XdlLibrary::close() {
    if (m_handle) {
        xdl_close(m_handle);
        m_handle = nullptr;
    }
    m_base = 0;
}

int ElfScannerManager::libNameToIndex(std::string_view libraryName) {
    for (const auto& entry : kLibTable) {
        if (entry.soName == libraryName)
            return entry.index;
    }
    return -1;
}

bool ElfScannerManager::scanAsync(const std::set<std::string>& libraries) {
    if (libraries.empty())
        return true;

    LOGI("[ElfScannerManager] Starting xDL scan for %zu libraries...", libraries.size());
    auto start = std::chrono::high_resolution_clock::now();

    struct ScanTask {
        int index;
        std::string name;
    };

    std::vector<ScanTask> tasks;
    tasks.reserve(libraries.size());
    for (const auto& libName : libraries) {
        int idx = libNameToIndex(libName);
        if (idx < 0) {
            LOGE("[ElfScannerManager] Unknown library: %s", libName.c_str());
            continue;
        }
        if (m_libraries[idx].isValid()) {
            LOGW("[ElfScannerManager] Library already opened: %s", libName.c_str());
            continue;
        }
        tasks.push_back({idx, libName});
    }

    std::vector<std::future<std::pair<int, XdlLibrary>>> futures;
    futures.reserve(tasks.size());
    for (const auto& task : tasks) {
        futures.push_back(std::async(std::launch::async, [task]() mutable {
            LOGI("[ElfScannerManager] xDL opening library: %s", task.name.c_str());
            XdlLibrary library;
            library.open(std::move(task.name));
            return std::make_pair(task.index, std::move(library));
        }));
    }

    bool allSuccess = true;
    for (size_t i = 0; i < futures.size(); ++i) {
        auto [idx, library] = futures[i].get();
        const auto& name = tasks[i].name;

        if (!library.isValid()) {
            LOGE("[ElfScannerManager] Failed to open library: %s", name.c_str());
            allSuccess = false;
            continue;
        }

        m_libraries[idx] = std::move(library);
        LOGI("[ElfScannerManager] %s base: 0x%lX", name.c_str(),
             static_cast<unsigned long>(m_libraries[idx].base()));
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    LOGI("[ElfScannerManager] xDL scan completed in %f ms", elapsed);

    return allSuccess;
}
