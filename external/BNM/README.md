# ByNameModding (BNM) - Android IL2CPP Modding Framework

ByNameModding (BNM) is a powerful, high-performance, and modern C++20 library designed for modding Android games built with the Unity engine (IL2CPP). 

Unlike traditional modding that relies on brittle memory offsets, BNM uses **Symbolic Modding**, allowing you to interact with game internals using class, method, and field names. This makes your mods stable across game updates and much easier to write and maintain.

## 🚀 Key Features
*   **Symbolic Resolution**: No more memory offsets! Use names like `Class("UnityEngine", "GameObject")`.
*   **High Performance**: Zero-allocation caching, Global Class Index, and Pool Allocators for extreme speed.
*   **Modern C++20 API**: Clean, type-safe syntax with universal invokers and RAII-based resource management.
*   **Unity 6 Support**: Full compatibility with the latest IL2CPP Metadata Version 39 (Unity 6000.x+).
*   **Runtime Class Management**: Create and modify C# classes directly from C++ at runtime.
*   **Coroutine Integration**: Seamlessly bridge C++20 coroutines with Unity's `IEnumerator`.
*   **Thread Safety**: Robust multi-threading support with automatic thread attachment and memory fences.

---

## 🛠️ Setup & Configuration

### 1. Build Prerequisites
*   **Android NDK**: Version supporting C++20 (r21e+ recommended).
*   **Hooking Backend**: BNM requires a hooking library. Recommended: [Dobby](https://github.com/ferly-chy/dobby) or [ShadowHook](https://github.com/bytedance/android-inline-hook).

### 2. Configuration
Edit `include/BNM/UserSettings/GlobalSettings.hpp`:
*   **Unity Version**: `#define UNITY_VER 233` (For Unity 6/6000.x+) or corresponding version.
*   **Hooking Library**: Uncomment `#define BNM_USE_DOBBY` or `#define BNM_USE_SHADOWHOOK`.

### 3. Initialization (JNI_OnLoad)
Initialize BNM in your library's entry point:

```cpp
#include <BNM/Loading.hpp>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);

    // 1. Load BNM
    BNM::Loading::TryLoadByJNI(env);

    // 2. Register your mod logic
    BNM::Loading::AddOnLoadedEvent([]() {
        BNM_LOG_INFO("BNM is ready!");
        // Your initialization code here
    });

    return JNI_VERSION_1_6;
}
```

---

## 📖 Usage Guide

### Using Helpers (Fastest Way)
Include `#include <BNM/Helpers.hpp>` for high-level macros:

```cpp
// Fast Class & Method lookup
auto myClass = BNM_CLASS("UnityEngine", "GameObject");
auto setActive = BNM_METHOD(myClass, "SetActive", 1);

// Fast Hooking
void (*old_Update)(void*);
void my_Update(void* instance) {
    old_Update(instance);
}
BNM_HOOK(BNM_CLASS("UnityEngine", "MonoBehaviour"), "Update", my_Update, old_Update);

// Fast Thread Attachment (for non-game threads)
void MyThread() {
    BNM_ATTACH_THREAD();
    // Use BNM safely here...
}
```

### Core API (Modern Syntax)

#### Classes & Objects
```cpp
using namespace BNM;

Class playerClass("UnityEngine", "Player");
auto playerInstance = playerClass.CreateNewInstance();
```

#### Methods (Universal Invoker)
```cpp
// Call any method directly with .Invoke<ReturnType>(args...)
auto getHealth = playerClass.GetMethod("GetHealth");
float health = getHealth[playerInstance].Invoke<float>();

// Call static method
auto physics = Class("UnityEngine", "Physics");
Vector3 gravity = physics.GetMethod("get_gravity").Invoke<Vector3>();
```

#### Fields & Properties
```cpp
// Type-safe Get/Set
auto healthField = playerClass.GetField("_health");
healthField[playerInstance].SetValue<float>(100.0f);
float val = healthField[playerInstance].GetValue<float>();

auto nameProp = playerClass.GetProperty("Name");
nameProp[playerInstance].SetValue<Structures::Mono::String*>(BNM::CreateMonoString("Hero"));
```

---

## 🏗️ Classes Management (Advanced)
BNM allows creating **New C# Classes** from C++:

```cpp
class MyComponent : public BNM::UnityEngine::MonoBehaviour {
    BNM_CustomClass(MyComponent, BNM::CompileTimeClassBuilder("MyMod", "SuperComponent"), Defaults::Get<UnityEngine::MonoBehaviour>(), nullptr);

    int score;
    BNM_CustomField(score, Defaults::Get<int>(), "score");

    void Start() { BNM_LOG_INFO("Component Started!"); }
    BNM_CustomMethod(Start, false, Defaults::Get<void>(), "Start");
};
```

---

## ⚠️ Stability & Performance Notes
*   **Memory Management**: BNM uses a high-performance **Metadata Pool Allocator** to prevent fragmentation.
*   **Caching**: All lookups are cached globally ($O(1)$) using zero-allocation transparent hashing.
*   **Thread Safety**: Always use `BNM_ATTACH_THREAD()` if accessing BNM/IL2CPP from a thread not created by Unity.
*   **Validity**: Always check if a Unity object is still "alive" using `obj->Alive()`.

---
*Generated for BNM Version 2.6.1-beta*
