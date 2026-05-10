#include "AndroidPlatform/AndroidPlatform.h"
#include "Core/ElfScannerManager.h"
#include "InputEvent/CustomHandleInput.h"
#include "InputEvent/InputEventHook.h"
#include "SwapChain/SwapChainHook.h"
#include "UI/DiagnosticsPanel.h"
#include "Utils/CrashHandler.h"
#include "Utils/FileLogger.h"
#include "Utils/HookUtils.h"
#include "Utils/ImAnime.hpp"
#include "Utils/Logger.h"
#include "imgui/imgui.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <BNM/Class.hpp>
#include <BNM/Field.hpp>
#include <BNM/Image.hpp>
#include <BNM/Loading.hpp>
#include <BNM/Method.hpp>

using namespace std::chrono_literals;

/**
 * BNM SETUP
 */

static float speedMultiplier = 1.0f;
static bool speedhackEnabled = false;
static bool espEnabled = false;
static bool espLines = false;
static bool espBoxes = true;

// Original pointer for the hook
static float (*old_Time_get_timeScale)();

// Hook function
static float my_Time_get_timeScale() {
  if (speedhackEnabled) {
    return old_Time_get_timeScale() * speedMultiplier;
  }
  return old_Time_get_timeScale();
}

static void OnIl2CppLoaded() {
  LOGI("BNM: libil2cpp loaded successfully.");

  auto assembly = BNM::Image("Assembly-CSharp");
  if (!assembly) {
    LOGW("BNM: Assembly-CSharp not found! This is normal for some games.");
  }

  // Hook UnityEngine.Time::get_timeScale
  auto timeClass = BNM::Class("UnityEngine", "Time");
  if (timeClass.IsValid()) {
    auto get_timeScale = timeClass.GetMethod("get_timeScale");
    if (get_timeScale.IsValid()) {
      BNM::BasicHook(get_timeScale, (void *)my_Time_get_timeScale,
                     (void **)&old_Time_get_timeScale);
      LOGI("BNM: Hooked UnityEngine.Time::get_timeScale");
    }
  }

  LOGI("BNM: Modding features applied.");
}

static void InitBNM() {
  LOGI("BNM: Waiting 10 seconds before initialization...");
  std::this_thread::sleep_for(std::chrono::seconds(10));

  BNM::Loading::AllowLateInitHook();
  BNM::Loading::AddOnLoadedEvent(OnIl2CppLoaded);

  /**
   * Wait for libil2cpp scanner
   */
  for (int i = 0; i < 100; ++i) {

    auto &scanner = Elf.il2cpp();

    if (scanner.isValid()) {
      LOGI("BNM: libil2cpp scanner found.");

      /**
       * Bind BNM method finder
       */
      BNM::Loading::SetMethodFinder(
          [](const char *name, void *userData) -> void * {
            auto library = static_cast<XdlLibrary *>(userData);
            if (!library)
              return nullptr;
            return library->findSymbol(name);
          },
          &scanner);

      /**
       * Load BNM
       */
      if (BNM::Loading::TryLoadByUsersFinder()) {
        LOGI("BNM: Initialized successfully.");
      } else {
        LOGE("BNM: Failed to initialize.");
      }

      return;
    }

    std::this_thread::sleep_for(200ms);
  }

  LOGE("BNM: libil2cpp.so not found after timeout.");
}

/**
 * MAIN THREAD
 */
void main_thread() {
  CrashHandler::Install();

  if (!Elf.scanAsync({
          //"libc.so",
          //"libUE4.so",
          "libunity.so",
          "libil2cpp.so",
          "libvulkan.so",
          "libinput.so",
          "libart.so",
      })) {
    LOGE("Failed to scan necessary libraries.");
  }

  /**
   * Start BNM loader thread
   */
  std::thread(InitBNM).detach();

  GetLogFile("Debug")->Append("Hello\n");

  /**
   * Render ImGui
   */
  SwapChainHook::SetRenderCallback([]() {
    ImGui::Begin("Unity Mod Menu");
    ImGuiFX::TextRainbow("IL2CPP Space");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Game Tweaks", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enable Speedhack", &speedhackEnabled);
      if (speedhackEnabled) {
        ImGui::SliderFloat("Speed Multiplier", &speedMultiplier, 0.1f, 10.0f);
      }
    }

    if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enable ESP", &espEnabled);
      if (espEnabled) {
        ImGui::SameLine();
        if (ImGui::Button("Settings"))
          ImGui::OpenPopup("esp_settings");

        if (ImGui::BeginPopup("esp_settings")) {
          ImGui::Checkbox("Boxes", &espBoxes);
          ImGui::Checkbox("Lines", &espLines);
          ImGui::EndPopup();
        }
      }
    }

    ImGui::Separator();

    DiagnosticsPanel::Render();

    ImGui::Separator();

    static bool demo = false;
    ImGui::Checkbox("Show ImGui Demo", &demo);
    if (demo) {
      ImGui::ShowDemoWindow(&demo);
    }

    ImGui::End();
  });

  /**
   * Install SwapChain hook
   */
  SwapChainHook::Install();

  /**
   * Input hook
   */
  InputEventHook::Initialize([](AInputEvent *event) {
    if (!event)
      return;

    if (SwapChainHook::IsInitialized()) {
      const ImVec2 size = {(float)SwapChainHook::GetWidth(),
                           (float)SwapChainHook::GetHeight()};

      CustomHandleInput::ImGui_ImplAndroid_HandleInputEvent(event, size);
    }

    int32_t event_type = AInputEvent_getType(event);

    if (event_type == AINPUT_EVENT_TYPE_KEY) {
      int32_t event_key_code = AKeyEvent_getKeyCode(event);
      int32_t event_action = AKeyEvent_getAction(event);

      if (event_key_code == AKEYCODE_VOLUME_DOWN &&
          event_action == AKEY_EVENT_ACTION_DOWN) {

        LOGI("keycode: AKEYCODE_VOLUME_DOWN, action: DOWN");

      } else if (event_key_code == AKEYCODE_VOLUME_UP &&
                 event_action == AKEY_EVENT_ACTION_DOWN) {

        LOGI("keycode: AKEYCODE_VOLUME_UP, action: DOWN");
      }
    }
  });
}

/**

 * JNI ENTRY

 */
static std::atomic<bool> g_Initialized{false};

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM *vm, void *reserved) {
  LOGI("JNI_OnLoad called (Manual or Injector).");

  if (!g_Initialized.exchange(true)) {
    std::thread(main_thread).detach();
  }

  return JNI_VERSION_1_6;
}

/**

 * CTOR / DTOR

 */
__attribute__((constructor)) void ctor() {
  LOGI("Library constructor called.");
}

__attribute__((destructor)) void dtor() { LOGI("dtor"); }
