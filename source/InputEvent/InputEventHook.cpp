#include "InputEventHook.h"

#include <android/api-level.h>
#include <android/input.h>
#include <array>
#include <cstddef>
#include <cstdint>

#include "Core/ElfScannerManager.h"
#include "Core/HookLifecycleManager.h"
#include "Utils/Logger.h"

namespace {

constexpr std::array<const char*, 3> kInputConsumerConsumeSymbols = {
    // Android 15 / API 35+
    "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE",
    // Android 13 / API 33 and several Android 14 vendor builds
    "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventEPiS7_Pb",
    // Older/common fallback without bool consumeBatches variants is intentionally resolved by xDL if exported.
    "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEPNS_10InputEventE",
};

static InputEventCallback_t g_InputEventCallback;
static int32_t (*Orig_consume)(void*, void*, bool, long, uint32_t*, AInputEvent**, int*, int*, bool*) = nullptr;

int32_t Fake_consume(void* thiz, void* factory, bool consumeBatches, long frameTime,
                     uint32_t* outSeq, AInputEvent** outEvent, int* a6, int* a7, bool* a8) {
    int32_t result = Orig_consume(thiz, factory, consumeBatches, frameTime, outSeq, outEvent, a6, a7, a8);
    if (g_InputEventCallback && outEvent && *outEvent)
        g_InputEventCallback(*outEvent);
    return result;
}

} // namespace

namespace InputEventHook
{
    void Initialize(InputEventCallback_t callback)
    {
        SetInputEventCallback(std::move(callback));

        int device_api_level = android_get_device_api_level();
        LOGI("[InputEventHook] android_get_device_api_level: %d", device_api_level);

        void* symbol = nullptr;
        const char* resolvedName = nullptr;
        for (const char* name : kInputConsumerConsumeSymbols) {
            symbol = Elf.input().findSymbol(name);
            if (symbol) {
                resolvedName = name;
                break;
            }
        }

        if (!symbol) {
            LOGE("[InputEventHook] Failed to find android::InputConsumer::consume symbol");
            return;
        }

        if (!HookLifecycleManager::GetInstance().install("android::InputConsumer::consume",
                                                         symbol,
                                                         reinterpret_cast<void*>(Fake_consume),
                                                         reinterpret_cast<void**>(&Orig_consume),
                                                         "InputEvent")) {
            LOGE("[InputEventHook] Hook install failed: symbol=%s addr=%p", resolvedName, symbol);
            Orig_consume = nullptr;
            return;
        }

        LOGI("[InputEventHook] Hooked android::InputConsumer::consume: %s @ %p", resolvedName, symbol);
    }

    void SetInputEventCallback(InputEventCallback_t callback)
    {
        g_InputEventCallback = std::move(callback);
    }
}
