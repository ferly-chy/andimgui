#include "AndroidPlatform/AndroidPlatform.h"
#include "Core/ElfScannerManager.h"
#include "InputEvent/CustomHandleInput.h"
#include "InputEvent/InputEventHook.h"
#include "SwapChain/SwapChainHook.h"
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

static void OnIl2CppLoaded() {
  LOGI("BNM: libil2cpp loaded successfully.");

  auto assembly = BNM::Image("Assembly-CSharp");
  if (!assembly) {
    LOGE("BNM: Assembly-CSharp not found!");
    return;
  }

  LOGI("BNM: Assembly-CSharp attached.");

  /**
   * Example:
   *
   * auto playerClass = BNM::Class("Game", "PlayerData", assembly);
   * if (playerClass.IsValid()) {
   *     LOGI("BNM: PlayerData class found.");
   * }
   */
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
            auto elf = static_cast<ElfScanner *>(userData);
            if (!elf)
              return nullptr;
            return reinterpret_cast<void *>(elf->findSymbol(name));
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

  KT::Init();

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

    static bool demo = false;
    ImGui::Checkbox("demo", &demo);

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
