#include "DiagnosticsPanel.h"

#include "AndroidPlatform/AndroidPlatform.h"
#include "Core/ElfScannerManager.h"
#include "Core/HookLifecycleManager.h"
#include "SwapChain/SwapChainHook.h"
#include "Utils/Logger.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <android/api-level.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if !defined(NDEBUG)
#define DIAGNOSTICS_SHOW_POINTERS 1
#else
#define DIAGNOSTICS_SHOW_POINTERS 0
#endif

namespace DiagnosticsPanel {
namespace {

std::string BaseCachePath() {
    const char* externalStorage = std::getenv("EXTERNAL_STORAGE");
    std::filesystem::path base = externalStorage && externalStorage[0] ? externalStorage : "/sdcard";
    return (base / "Android" / "data" / getprogname() / "cache" / kPROJECT_NAME).string();
}

void RenderDobbyResult(HookLifecycleManager::State state, int result) {
    if (state == HookLifecycleManager::State::Pending ||
        state == HookLifecycleManager::State::InvalidRequest) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "N/A");
        return;
    }

    if (result == 0) {
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "OK (0)");
        return;
    }

    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "ERR (%d)", result);
}

void RenderLibraryStatus(const char* label, const XdlLibrary& library) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(library.isValid() ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
                                         : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                       "%s", library.isValid() ? "loaded" : "missing");
#if DIAGNOSTICS_SHOW_POINTERS
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("0x%lX", static_cast<unsigned long>(library.base()));
#endif
}

void RenderDiagnosticsTab() {
    ImGui::TextUnformatted("Runtime");
    ImGui::Separator();
    ImGui::Text("Android API: %d", android_get_device_api_level());
    ImGui::Text("Surface: %d x %d", SwapChainHook::GetWidth(), SwapChainHook::GetHeight());
    ImGui::Text("SwapChain initialized: %s", SwapChainHook::IsInitialized() ? "yes" : "no");
#if DIAGNOSTICS_SHOW_POINTERS
    ImGui::Text("JavaVM: %p", AndroidPlatform::GetJavaVM());
    ImGui::Text("NativeWindow: %p", AndroidPlatform::GetNativeWindow());
#endif
    ImGui::Text("Log root: %s", ResolveLogRoot().c_str());

    ImGui::Spacing();
    ImGui::TextUnformatted("ELF libraries");
    if (ImGui::BeginTable("diag_libs", DIAGNOSTICS_SHOW_POINTERS ? 3 : 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Library");
        ImGui::TableSetupColumn("Status");
#if DIAGNOSTICS_SHOW_POINTERS
        ImGui::TableSetupColumn("Base");
#endif
        ImGui::TableHeadersRow();
        RenderLibraryStatus("libunity.so", Elf.unity());
        RenderLibraryStatus("libil2cpp.so", Elf.il2cpp());
        RenderLibraryStatus("libvulkan.so", Elf.vulkan());
        RenderLibraryStatus("libEGL.so", Elf.egl());
        RenderLibraryStatus("libinput.so", Elf.input());
        RenderLibraryStatus("libart.so", Elf.art());
        ImGui::EndTable();
    }
}

void RenderHooksTab() {
    const auto hooks = HookLifecycleManager::GetInstance().snapshot();
    ImGui::Text("Installed hooks: %zu / %zu", HookLifecycleManager::GetInstance().installedCount(), hooks.size());
    ImGui::Separator();

    if (ImGui::BeginTable("hook_lifecycle", DIAGNOSTICS_SHOW_POINTERS ? 8 : 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Owner");
        ImGui::TableSetupColumn("Backend");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Dobby Result");
#if DIAGNOSTICS_SHOW_POINTERS
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Original");
#endif
        ImGui::TableHeadersRow();

        for (const auto& hook : hooks) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", static_cast<unsigned long long>(hook.sequence));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(hook.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(hook.owner.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(std::string{HookLifecycleManager::backendName(hook.backend)}.c_str());
            ImGui::TableSetColumnIndex(4);
            const bool installed = hook.state == HookLifecycleManager::State::Installed;
            const bool failed = hook.state == HookLifecycleManager::State::Failed ||
                                hook.state == HookLifecycleManager::State::InvalidRequest ||
                                hook.state == HookLifecycleManager::State::DestroyFailed;
            const ImVec4 stateColor = installed ? ImVec4(0.35f, 0.95f, 0.45f, 1.0f)
                                      : failed ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
                                               : ImVec4(0.95f, 0.65f, 0.25f, 1.0f);
            ImGui::TextColored(stateColor, "%s", std::string{HookLifecycleManager::stateName(hook.state)}.c_str());
            ImGui::TableSetColumnIndex(5);
            RenderDobbyResult(hook.state, hook.installResult);
#if DIAGNOSTICS_SHOW_POINTERS
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%p", hook.target);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%p", hook.original ? *hook.original : nullptr);
#endif
        }

        ImGui::EndTable();
    }
}

void RenderCrashLogsTab() {
    static std::vector<std::string> crashLogs;
    static int selected = -1;
    static std::string selectedContent;

    if (ImGui::Button("Refresh Crash Logs")) {
        crashLogs = ListCrashLogs();
        selected = crashLogs.empty() ? -1 : 0;
        selectedContent = selected >= 0 ? ReadCrashLogTail(crashLogs[static_cast<std::size_t>(selected)]) : std::string{};
    }

    ImGui::SameLine();
    ImGui::Text("%zu log(s)", crashLogs.size());
    ImGui::Text("Path: %s", (ResolveLogRoot() + "/CrashLog").c_str());
    ImGui::Separator();

    ImGui::BeginChild("crash_log_list", ImVec2(0, 120), true);
    for (std::size_t i = 0; i < crashLogs.size(); ++i) {
        const auto label = std::filesystem::path(crashLogs[i]).filename().string();
        if (ImGui::Selectable(label.c_str(), selected == static_cast<int>(i))) {
            selected = static_cast<int>(i);
            selectedContent = ReadCrashLogTail(crashLogs[i]);
        }
    }
    ImGui::EndChild();

    if (selected >= 0 && selected < static_cast<int>(crashLogs.size())) {
        ImGui::Text("Selected: %s", crashLogs[static_cast<std::size_t>(selected)].c_str());
    }

    ImGui::BeginChild("crash_log_content", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (selectedContent.empty()) {
        ImGui::TextUnformatted("No crash log selected or file is empty.");
    } else {
        ImGui::TextUnformatted(selectedContent.c_str());
    }
    ImGui::EndChild();
}

} // namespace

std::string ResolveLogRoot() {
    return BaseCachePath();
}

std::vector<std::string> ListCrashLogs() {
    std::vector<std::string> files;
    const std::filesystem::path crashDir = std::filesystem::path{ResolveLogRoot()} / "CrashLog";
    std::error_code ec;
    if (!std::filesystem::exists(crashDir, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(crashDir, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec)) {
            files.push_back(entry.path().string());
        }
    }

    std::ranges::sort(files, [](const std::string& lhs, const std::string& rhs) {
        std::error_code ecL;
        std::error_code ecR;
        const auto leftTime = std::filesystem::last_write_time(lhs, ecL);
        const auto rightTime = std::filesystem::last_write_time(rhs, ecR);
        if (ecL || ecR) {
            return lhs > rhs;
        }
        return leftTime > rightTime;
    });

    return files;
}

std::string ReadCrashLogTail(const std::string& path, std::size_t maxBytes) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    const auto size = file.tellg();
    if (size <= 0) {
        return {};
    }

    const auto readSize = std::min<std::size_t>(static_cast<std::size_t>(size), maxBytes);
    file.seekg(-static_cast<std::streamoff>(readSize), std::ios::end);

    std::string content(readSize, '\0');
    file.read(content.data(), static_cast<std::streamsize>(readSize));
    content.resize(static_cast<std::size_t>(file.gcount()));
    return content;
}

void Render() {
    if (!ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (ImGui::BeginTabBar("diagnostics_tabs")) {
        if (ImGui::BeginTabItem("Runtime")) {
            RenderDiagnosticsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hooks")) {
            RenderHooksTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Crash Logs")) {
            RenderCrashLogsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace DiagnosticsPanel
