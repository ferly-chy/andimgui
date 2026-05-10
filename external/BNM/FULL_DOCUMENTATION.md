# ByNameModding (BNM) - Full Documentation

ByNameModding (BNM) is a high-performance **C++23** framework for Android Unity IL2CPP modding. It lets native mods resolve and use IL2CPP images, classes, methods, fields, properties, events, managed objects, Unity value types, runtime classes, and coroutines by name instead of hard-coded offsets.

This document targets the current public API in this repository: **BNM `2.6.1-beta`**.

---

## 1. Core Idea: Symbolic IL2CPP Modding

Traditional IL2CPP mods often call functions by fixed addresses such as `libil2cpp.so + 0x123456`. Those offsets usually change after every game update.

BNM resolves metadata symbols instead:

```cpp
BNM::Class playerClass("", "PlayerController");
auto setHealth = playerClass.GetMethod("SetHealth", 1);
setHealth[playerInstance].Invoke<void>(999.0f);
```

Benefits:

- **Update resilience**: code keeps working when metadata names stay stable.
- **Readability**: code names the target class, field, method, or property directly.
- **Safer API surface**: typed wrappers validate null/invalid states and log diagnostics when enabled.
- **Runtime integration**: BNM can create/modify managed classes and bridge C++ coroutines to Unity `IEnumerator`.

---

## 2. Requirements and Build Configuration

### Language and platform

BNM now requires **C++23 or newer**. `include/BNM/UserSettings/GlobalSettings.hpp` contains:

```cpp
#if __cplusplus < 202302L
static_assert(false, "ByNameModding requires C++23 and above!");
#endif
```

The project build files also set C++23:

- `CMakeLists.txt`: `set(CMAKE_CXX_STANDARD 23)`
- `Android.mk`: `LOCAL_CPPFLAGS += -std=c++23 ...`

### Android/NDK

BNM is designed for Android IL2CPP games. Use an Android NDK/toolchain that supports C++23 well enough for this project.

### Hooking backend

BNM is not a standalone inline-hook engine. It wraps a user-selected backend through `BasicHook(...)` and `Unhook(...)` in `GlobalSettings.hpp`.

Current built-in choices:

```cpp
// #define BNM_USE_DOBBY
// #define BNM_USE_SHADOWHOOK
```

- Define **one** of these when you want the provided Dobby or ShadowHook wrappers.
- If neither is defined, BNM compiles dummy fallback hooks that return `nullptr`/do nothing. That is useful for compilation but not for real inline hooking.
- `Loading::AllowLateInitHook()` requires a real `Unhook`, otherwise late-init hooks can slow the game down.

### Unity version selection

Configure the target Unity/IL2CPP version in `include/BNM/UserSettings/GlobalSettings.hpp`:

```cpp
#define UNITY_VER 222
#define UNITY_PATCH_VER 32
```

Supported version constants in the current settings include:

| Unity version | `UNITY_VER` |
|---|---:|
| 5.6.4f1 | `56` |
| 2017.1.x | `171` |
| 2017.2.x - 2017.4.x | `172` |
| 2018.1.x | `181` |
| 2018.2.x | `182` |
| 2018.3.x - 2018.4.x | `183` |
| 2019.1.x - 2019.2.x | `191` |
| 2019.3.x | `193` |
| 2019.4.x | `194` |
| 2020.1.x | `201` |
| 2020.2.x - 2020.3.19 | `202` |
| 2020.3.20+ | `203` |
| 2021.1.x | `211` |
| 2021.2.x | `212` |
| 2021.3.x | `213` |
| 2022.1.x | `221` |
| 2022.2.x - 2022.3.x | `222` |
| 2023.1.x | `231` |
| 2023.2.x+ | `232` |
| Unity 6 / 6000.x+ | `233` |

For Unity `2021.1.x` (`UNITY_VER == 211`), `UNITY_PATCH_VER` must be defined.

### Feature macros

Important settings in `GlobalSettings.hpp`:

```cpp
#define BNM_ALLOW_MULTI_THREADING_SYNC
#define BNM_CLASSES_MANAGEMENT
#define BNM_COROUTINE
#define BNM_USE_IL2CPP_ALLOCATOR
#define BNM_ALLOW_STR_METHODS
#define BNM_ALLOW_SAFE_IS_ALLOCATED
#define BNM_ALLOW_SELF_CHECKS
#define BNM_CHECK_INSTANCE_TYPE
```

Notes:

- `BNM_CLASSES_MANAGEMENT` enables runtime class creation/modification.
- `BNM_COROUTINE` enables coroutine wrappers and requires ClassesManagement.
- `BNM_DOTNET35` switches `Dictionary<TKey, TValue>` layout for old .NET 3.5 games.
- `BNM_OBFUSCATE(str)` and `BNM_OBFUSCATE_TMP(str)` are customization points for string encryption/obfuscation.

---

## 3. Initialization and Lifecycle

Include the loading API:

```cpp
#include <BNM/Loading.hpp>
```

Typical initialization from `JNI_OnLoad`:

```cpp
#include <jni.h>
#include <BNM/Loading.hpp>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env{};
    vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);

    BNM::Loading::TryLoadByJNI(env);

    BNM::Loading::AddOnLoadedEvent([]() {
        BNM_LOG_INFO("BNM and IL2CPP are ready");
    });

    return JNI_VERSION_1_6;
}
```

`AddOnLoadedEvent` callbacks run from the IL2CPP thread after BNM and IL2CPP are fully loaded.

### Loading methods

BNM provides several loading paths:

```cpp
bool BNM::Loading::TryLoadByJNI(JNIEnv *env, jobject context = nullptr);
bool BNM::Loading::TryLoadByDlfcnHandle(void *handle);
void BNM::Loading::SetMethodFinder(MethodFinder finderMethod, void *userData);
bool BNM::Loading::TryLoadByUsersFinder();
void BNM::Loading::TrySetupByUsersFinder();
void BNM::Loading::AllowLateInitHook();
void BNM::Loading::AddOnLoadedEvent(void (*event)());
void BNM::Loading::ClearOnLoadedEvents();
```

Use cases:

- `TryLoadByJNI`: common Android path; uses JNI/context to locate `libil2cpp.so`.
- `TryLoadByDlfcnHandle`: use when you already have a `dlopen` handle for `libil2cpp.so`.
- `SetMethodFinder` + `TryLoadByUsersFinder`: use custom symbol resolution when JNI/dlfcn are unsuitable.
- `TrySetupByUsersFinder`: direct setup from the current thread after IL2CPP is fully loaded; avoid calling it too early or from non-IL2CPP threads.
- `AllowLateInitHook`: allows late loading by hooking `il2cpp::vm::Class::FromIl2CppType`; call it before a `TryLoad...` method if you need late initialization.

### Thread attachment

For threads not created by Unity/IL2CPP:

```cpp
BNM::AttachIl2Cpp();
// use BNM/IL2CPP APIs
BNM::DetachIl2Cpp();
```

Or with the helper macro from `BNM/Helpers.hpp`:

```cpp
#include <BNM/Helpers.hpp>

void MyThreadEntry() {
    BNM_ATTACH_THREAD();
    // Detached automatically when the local RAII helper leaves scope.
}
```

Other utilities:

```cpp
bool BNM::IsLoaded();
BNM::IL2CPP::Il2CppThread *BNM::CurrentIl2CppThread();
void *BNM::GetIl2CppLibraryHandle();
```

---

## 4. Headers and Namespaces

Common includes:

```cpp
#include <BNM/Loading.hpp>
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/Field.hpp>
#include <BNM/Property.hpp>
#include <BNM/Event.hpp>
#include <BNM/Defaults.hpp>
#include <BNM/BasicMonoStructures.hpp>
#include <BNM/ComplexMonoStructures.hpp>
#include <BNM/UnityStructures.hpp>
#include <BNM/Helpers.hpp>
```

Main namespaces:

```cpp
namespace BNM;
namespace BNM::Loading;
namespace BNM::Defaults;
namespace BNM::Structures::Mono;
namespace BNM::Structures::Unity;
namespace BNM::UnityEngine;
namespace BNM::Coroutine;
```

---

## 5. Images and Classes

### Images

`BNM::Image` represents an IL2CPP image/assembly.

```cpp
BNM::Image image("Assembly-CSharp.dll");
if (image) {
    auto count = image.GetClassesCount();
    auto classes = image.GetClasses(true); // include inner classes
}

for (auto img : BNM::Image::GetImages()) {
    BNM_LOG_INFO("Image: %s", std::string(img.str()).c_str());
}
```

Main APIs:

```cpp
BNM::Image();
BNM::Image(const BNM::IL2CPP::Il2CppImage *image);
BNM::Image(const std::string_view &name);
BNM::Image(const BNM::IL2CPP::Il2CppAssembly *assembly);
std::vector<BNM::Class> GetClasses(bool includeInner = false) const;
uint32_t GetClassesCount() const;
BNM::IL2CPP::Il2CppImage *GetInfo() const;
bool IsValid() const;
static std::vector<BNM::Image> GetImages();
```

### Classes

`BNM::Class` is the entry point for most metadata operations.

Name lookup uses **namespace + class name**. For classes in the global namespace, pass an empty namespace:

```cpp
BNM::Class playerClass("", "PlayerController");
BNM::Class gameObjectClass("UnityEngine", "GameObject");
```

Constructors include:

```cpp
BNM::Class empty;
BNM::Class fromClass(il2cppClass);
BNM::Class fromObject(il2cppObject);
BNM::Class fromType(il2cppType);
BNM::Class fromMonoType(monoType);
BNM::Class fromCompileTime(compileTimeClass);
BNM::Class byName("UnityEngine", "GameObject");
BNM::Class byImage("", "PlayerController", BNM::Image("Assembly-CSharp.dll"));
```

Validation and conversion:

```cpp
if (byName) {
    BNM::IL2CPP::Il2CppClass *klass = byName.GetClass();
    BNM::IL2CPP::Il2CppType *type = byName.GetIl2CppType();
    BNM::MonoType *monoType = byName.GetMonoType();
    BNM::Image image = byName.GetImage();
}
```

Metadata discovery:

```cpp
auto parent = klass.GetParent();
auto fields = klass.GetFields();
auto methods = klass.GetMethods();
auto properties = klass.GetProperties();
auto events = klass.GetEvents();
auto interfaces = klass.GetInterfaces();
auto inner = klass.GetInnerClasses();
```

Span APIs expose low-level metadata arrays without building vectors:

```cpp
auto methodSpan = klass.GetMethodsSpan();
auto fieldSpan = klass.GetFieldsSpan();
auto interfaceSpan = klass.GetInterfacesSpan();
auto innerSpan = klass.GetInnerClassesSpan();
```

Lookup APIs:

```cpp
auto methodByCount = klass.GetMethod("SetHealth", 1);
auto methodByParamNames = klass.GetMethod("DoSomething", {"id", "name"});
auto methodByParamTypes = klass.GetMethod("SetPosition", {BNM::Defaults::Get<BNM::Structures::Unity::Vector3>()});

auto field = klass.GetField("_health");
auto property = klass.GetProperty("Health");
auto typedProperty = klass.GetProperty("Health", BNM::Defaults::Get<float>());
auto eventInfo = klass.GetEvent("OnDeath");
auto innerClass = klass.GetInnerClass("NestedType");
```

Class transforms and generic classes:

```cpp
auto arrayClass = klass.GetArray();
auto pointerClass = klass.GetPointer();
auto referenceClass = klass.GetReference();

auto listDef = BNM::Class("System.Collections.Generic", "List`1");
auto intList = listDef.GetGeneric({BNM::Defaults::Get<int>()});
```

Object creation and boxing:

```cpp
BNM::IL2CPP::Il2CppObject *obj = klass.CreateNewInstance(); // no constructor call
BNM::IL2CPP::Il2CppObject *objWithCtor = klass.CreateNewObjectParameters(42, true);
BNM::IL2CPP::Il2CppObject *objWithNamedCtor = klass.CreateNewObjectTypes({"id", "active"}, 42, true);

auto *arr = klass.NewArray<void *>(10);
auto *list = klass.NewList<void *>();
auto *bnmList = klass.NewListBNM<void *>(); // advanced: BNM-managed list backing

auto boxedInt = BNM::Defaults::Get<int>().ToClass().BoxObject(123);
auto boxedViaDefaults = BNM::Defaults::Box(123);
```

---

## 6. Methods

`GetMethod(...)` returns `BNM::MethodBase`. Use either the modern universal invoker or cast to a typed `BNM::Method<Ret>`.

### Modern invocation

```cpp
auto playerClass = BNM::Class("", "PlayerController");
auto getHealth = playerClass.GetMethod("GetHealth", 0);
auto setHealth = playerClass.GetMethod("SetHealth", 1);

float health = getHealth[playerInstance].Invoke<float>();
setHealth[playerInstance].Invoke<void>(999.0f);
```

Static methods do not need an instance:

```cpp
auto physics = BNM::Class("UnityEngine", "Physics");
auto getGravity = physics.GetMethod("get_gravity", 0);
auto gravity = getGravity.Invoke<BNM::Structures::Unity::Vector3>();
```

### Typed method API

```cpp
BNM::Method<float> getHealth = playerClass.GetMethod("GetHealth", 0);
float health = getHealth[playerInstance]();

BNM::Method<void> setHealth = playerClass.GetMethod("SetHealth", 1);
setHealth[playerInstance](250.0f);
```

You can also cast from `MethodBase`:

```cpp
auto method = playerClass.GetMethod("SetHealth", 1);
method.cast<void>()[playerInstance](250.0f);
```

### Generic methods

```cpp
auto genericMethod = someClass.GetMethod("GetComponent", 0);
auto typedMethod = genericMethod.GetGeneric({BNM::Defaults::Get<BNM::UnityEngine::MonoBehaviour *>()});
auto *component = typedMethod[gameObject].Invoke<BNM::UnityEngine::MonoBehaviour *>();
```

### Method metadata

```cpp
if (method.IsValid()) {
    auto *info = method.GetInfo();
    auto offset = method.GetOffset();
    auto returnType = method.GetReturnType();
    auto parent = method.GetParentClass();
    auto overrideMethod = method.GetOverride();
}
```

---

## 7. Fields

`GetField(...)` returns `BNM::FieldBase`.

### Modern field access

```cpp
auto healthField = playerClass.GetField("_health");

float health = healthField[playerInstance].GetValue<float>();
healthField[playerInstance].SetValue<float>(500.0f);
```

Static fields do not need an instance:

```cpp
auto instanceField = BNM::Class("", "GameManager").GetField("Instance");
auto *manager = instanceField.GetValue<BNM::IL2CPP::Il2CppObject *>();
```

### Typed field API

```cpp
BNM::Field<float> health = playerClass.GetField("_health");
float current = health[playerInstance].Get();
health[playerInstance].Set(500.0f);

// Operator aliases
current = health[playerInstance];
health[playerInstance] = 250.0f;
```

### Field metadata and pointers

```cpp
if (healthField.IsValid()) {
    auto *info = healthField.GetInfo();
    auto offset = healthField.GetOffset();
    auto type = healthField.GetType();
    auto parent = healthField.GetParentClass();
    void *ptr = healthField[playerInstance].GetFieldPointer();
}
```

`GetFieldPointer()` is useful for direct access but does not support thread-static fields.

---

## 8. Properties

`GetProperty(...)` returns `BNM::PropertyBase`.

### Modern property access

```cpp
auto nameProperty = playerClass.GetProperty("Name");

auto *name = nameProperty[playerInstance].GetValue<BNM::Structures::Mono::String *>();
nameProperty[playerInstance].SetValue<BNM::Structures::Mono::String *>(BNM::CreateMonoString("Hero"));
```

### Typed property API

```cpp
BNM::Property<float> speed = playerClass.GetProperty("Speed");
float currentSpeed = speed[playerInstance].Get();
speed[playerInstance].Set(12.5f);

// Operator aliases
currentSpeed = speed[playerInstance];
speed[playerInstance] = 20.0f;
```

Properties call their managed getter/setter methods internally. If a getter or setter is missing, BNM logs an error and returns a default value for getters.

---

## 9. Events and Delegates

`GetEvent(...)` returns `BNM::EventBase`; `BNM::Event<Ret, Parameters...>` provides typed access.

```cpp
auto onDeathBase = playerClass.GetEvent("OnDeath");
BNM::Event<void> onDeath = onDeathBase;

// delegate creation depends on the delegate target/method you want to use.
// Once you have a BNM::Delegate<Ret>*, add/remove it like this:
onDeath[playerInstance].Add(delegatePtr);
onDeath[playerInstance].Remove(delegatePtr);

// If the event exposes a raise method:
onDeath[playerInstance].Raise();
```

Operator aliases:

```cpp
onDeath[playerInstance] += delegatePtr;
onDeath[playerInstance] -= delegatePtr;
onDeath[playerInstance]();
```

---

## 10. Hooking

BNM exposes three method-hooking approaches. Choose based on how the target method is called and what you need to intercept.

### 10.1 BasicHook: inline/backend hook

`BNM::BasicHook` hooks the native method pointer using the configured backend (`BNM_USE_DOBBY`, `BNM_USE_SHADOWHOOK`, or your own implementation).

```cpp
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>

void (*old_Update)(BNM::IL2CPP::Il2CppObject *instance);

void my_Update(BNM::IL2CPP::Il2CppObject *instance) {
    if (old_Update) old_Update(instance);
}

void InstallHooks() {
    auto update = BNM::Class("", "PlayerController").GetMethod("Update", 0);
    BNM::BasicHook(update, my_Update, old_Update);
}
```

Important: the hook signature must match the native IL2CPP calling convention for that Unity version and target method. For instance methods, the object instance is the first argument.

### 10.2 InvokeHook: MethodInfo/invoker hook

`BNM::InvokeHook` changes the `MethodInfo` path. It is useful for Unity messages and other calls made through IL2CPP invoke APIs. It has near-zero inline-hook overhead but does not intercept every direct native call.

```cpp
void (*old_Start)(BNM::IL2CPP::Il2CppObject *instance);

void my_Start(BNM::IL2CPP::Il2CppObject *instance) {
    if (old_Start) old_Start(instance);
}

void InstallInvokeHook() {
    auto start = BNM::Class("", "PlayerController").GetMethod("Start", 0);
    BNM::InvokeHook(start, my_Start, old_Start);
}
```

### 10.3 VirtualHook: vtable hook for virtual methods

`BNM::VirtualHook` changes the virtual table for a target class.

```cpp
void (*old_ToString)(BNM::IL2CPP::Il2CppObject *instance);

void my_ToString(BNM::IL2CPP::Il2CppObject *instance) {
    if (old_ToString) old_ToString(instance);
}

void InstallVirtualHook() {
    auto targetClass = BNM::Class("", "SomeDerivedType");
    auto method = targetClass.GetMethod("ToString", 0);
    BNM::VirtualHook(targetClass, method, my_ToString, old_ToString);
}
```

### 10.4 Helper macros

`BNM/Helpers.hpp` provides convenience macros:

```cpp
#include <BNM/Helpers.hpp>

auto gameObjectClass = BNM_CLASS("UnityEngine", "GameObject");
auto setActive = BNM_METHOD(gameObjectClass, "SetActive", 1);
auto nameField = BNM_FIELD(gameObjectClass, "m_Name");
auto activeProp = BNM_PROPERTY(gameObjectClass, "activeSelf");

BNM_HOOK(BNM_CLASS("", "PlayerController"), "Update", my_Update, old_Update);
BNM_INVOKE_HOOK(BNM_CLASS("", "PlayerController"), "Start", my_Start, old_Start);
```

The helper macros are thin wrappers around the core API.

---

## 11. Managed Mono Structures

BNM mirrors common managed structures in `BNM::Structures::Mono`.

### String

```cpp
auto *mono = BNM::CreateMonoString("hello");
std::string utf8 = mono->str();
unsigned int hash = mono->GetHash();

auto *empty = BNM::Structures::Mono::String::Empty();
bool emptyOrNull = mono->IsNullOrEmpty();
```

### Array

```cpp
auto intClass = BNM::Defaults::Get<int>().ToClass();
auto *array = intClass.NewArray<int>(3);

array->At(0) = 10;
array->At(1) = 20;
array->At(2) = 30;

int value = array->At(1);
auto size = array->GetSize();
auto data = array->GetData();
auto vector = array->ToVector();
```

Aliases and helpers:

```cpp
array->GetCapacity();
array->CopyFrom(vector);
array->CopyTo(rawPointer);
array->operator[](index);
```

### List

`BNM::Structures::Mono::List<T>` maps managed `System.Collections.Generic.List<T>`.

```cpp
auto *list = BNM::Defaults::Get<int>().ToClass().NewList<int>();
list->Add(10);
list->Add(20);
int count = list->GetSize();
std::vector<int> values = list->ToVector();
list->Clear();
```

Common operations include `GetData()`, `GetSize()`, `GetCapacity()`, `GetVersion()`, `ToVector()`, `Add()`, `Remove()`, `RemoveAt()`, `Clear()`, `Contains()`, `At(index)`, `operator[]`, `CopyFrom()`, and resizing helpers.

### Dictionary

`BNM::Structures::Mono::Dictionary<TKey, TValue>` is in `ComplexMonoStructures.hpp`.

```cpp
auto *dict = /* managed Dictionary<TKey, TValue>* */;

if (dict->ContainsKey(key)) {
    auto value = dict->Get(key);
}

dict->Add(key, value);
dict->Insert(key, newValue); // set_Item
dict->Remove(key);
dict->Clear();

auto keys = dict->GetKeys();
auto values = dict->GetValues();
auto map = dict->ToMap();
```

For old .NET 3.5 games, define `BNM_DOTNET35` so the dictionary layout matches that runtime.

---

## 12. Unity Structures and UnityEngine Objects

Unity math/value types are under `BNM::Structures::Unity` and are included through `BNM/UnityStructures.hpp`:

- `Vector2`
- `Vector3`
- `Vector4`
- `Color`
- `Color32`
- `Ray`
- `RaycastHit`
- `RaycastHit2D`
- `Quaternion`
- `Matrix3x3`
- `Matrix4x4`
- `Rect`

Example:

```cpp
using BNM::Structures::Unity::Vector3;

Vector3 position{1.0f, 2.0f, 3.0f};
auto transform = BNM::Class("UnityEngine", "Transform");
auto setPosition = transform.GetMethod("set_position", 1);
setPosition[transformInstance].Invoke<void>(position);
```

Unity object wrappers live in `BNM::UnityEngine`:

```cpp
BNM::UnityEngine::Object *obj = /* ... */;
if (obj && obj->Alive()) {
    // object still exists on the Unity side
}
```

Always validate Unity objects before using cached pointers. Managed Unity objects can be destroyed while native memory still appears non-null.

---

## 13. Defaults and Type Mapping

`BNM::Defaults::Get<T>()` returns a `DefaultTypeRef` for common C#/Unity types.

Supported mappings include:

- `void` -> `System.Void`
- `bool` -> `System.Boolean`
- `BNM::Types::byte` / `sbyte`
- `short`, `BNM::Types::ushort`
- `int`, `BNM::Types::uint`
- `BNM::Types::nint`, `BNM::Types::nuint`
- `long`, `BNM::Types::ulong`
- `float`, `double`, `BNM::Types::decimal`
- `BNM::IL2CPP::Il2CppString *` / `BNM::Structures::Mono::String *`
- Unity value types such as `Vector2`, `Vector3`, `Quaternion`, `Color`, etc.
- `BNM::UnityEngine::Object *`
- `BNM::UnityEngine::MonoBehaviour *`
- other pointers -> `System.Object`

Examples:

```cpp
auto intType = BNM::Defaults::Get<int>();
auto stringType = BNM::Defaults::Get<BNM::Structures::Mono::String *>();
auto vectorType = BNM::Defaults::Get<BNM::Structures::Unity::Vector3>();

auto intClass = intType.ToClass();
BNM::CompileTimeClass intCompileTime = intType;

auto boxed = BNM::Defaults::Box(123);
```

---

## 14. CompileTimeClass and Generics

BNM uses `BNM::CompileTimeClass` to describe types before or during metadata setup. It is required by APIs such as class/method generics and ClassesManagement macros.

Common sources:

```cpp
BNM::CompileTimeClass intType = BNM::Defaults::Get<int>();
BNM::CompileTimeClass playerType = BNM::Class("", "PlayerController");
BNM::CompileTimeClass built = BNM::CompileTimeClassBuilder("MyMod", "MyComponent");
```

Generic class example:

```cpp
auto listDef = BNM::Class("System.Collections.Generic", "List`1");
auto listOfInt = listDef.GetGeneric({BNM::Defaults::Get<int>()});
```

Generic method example:

```cpp
auto method = someClass.GetMethod("SomeGenericMethod", 0);
auto methodOfInt = method.GetGeneric({BNM::Defaults::Get<int>()});
methodOfInt[someInstance].Invoke<void>();
```

---

## 15. ClassesManagement: Runtime Class Creation and Modification

Enable with:

```cpp
#define BNM_CLASSES_MANAGEMENT
```

ClassesManagement lets C++ classes be exposed as valid IL2CPP/C# classes, add fields/methods, and override or hook existing methods.

### New component example

```cpp
#include <BNM/ClassesManagement.hpp>
#include <BNM/Defaults.hpp>
#include <BNM/UnityStructures.hpp>

class MyComponent : public BNM::UnityEngine::MonoBehaviour {
    BNM_CustomClass(
        MyComponent,
        BNM::CompileTimeClassBuilder("MyMod", "MyComponent"),
        BNM::Defaults::Get<BNM::UnityEngine::MonoBehaviour *>(),
        nullptr
    );

    int score{};
    BNM_CustomField(score, BNM::Defaults::Get<int>(), "score");

    void Start() {
        BNM_LOG_INFO("MyComponent started");
    }
    BNM_CustomMethod(Start, false, BNM::Defaults::Get<void>(), "Start");

    void Update() {
        ++score;
    }
    BNM_CustomMethod(Update, false, BNM::Defaults::Get<void>(), "Update");
};
```

Macro reference:

```cpp
BNM_CustomClass(_class_, _targetType_, _baseType_, _owner_, ...interfaces)
BNM_CustomField(_field_, _type_, _name_)
BNM_CustomMethod(_method_, _isStatic_, _returnType_, _name_, ...parameterTypes)
BNM_CustomMethodMarkAsInvokeHook(_method_)
BNM_CustomMethodMarkAsBasicHook(_method_)
BNM_CustomMethodSkipTypeMatch(_method_)
BNM_CustomMethodCopyAttributes(_method_, _copy_name_)
BNM_CallCustomMethodOrigin(_method_, ...args)
```

If your custom method replaces or overrides an existing method, BNM can keep an origin pointer. Call it with:

```cpp
BNM_CallCustomMethodOrigin(Update, this);
```

Use markers to prefer a specific hook strategy:

```cpp
BNM_CustomMethodMarkAsInvokeHook(Update);
BNM_CustomMethodMarkAsBasicHook(Update);
BNM_CustomMethodSkipTypeMatch(Update);
BNM_CustomMethodCopyAttributes(Update, "Update");
```

Advanced runtime API:

```cpp
BNM::ClassesManagement::ProcessClassRuntime(customClassPtr);
```

This must be used on the main IL2CPP thread.

---

## 16. Coroutines

Enable with both:

```cpp
#define BNM_CLASSES_MANAGEMENT
#define BNM_COROUTINE
```

Include:

```cpp
#include <BNM/Coroutine.hpp>
```

Coroutine example:

```cpp
BNM::Coroutine::IEnumerator MyCoroutine() {
    BNM_LOG_INFO("Before wait");
    co_yield BNM::Coroutine::WaitForSeconds(2.0f);
    BNM_LOG_INFO("After wait");
}
```

To pass it to Unity, use `Get()` or the conversion/operator on `IEnumerator`:

```cpp
auto startCoroutine = monoBehaviourClass.GetMethod("StartCoroutine", 1);
startCoroutine[monoBehaviourInstance].Invoke<BNM::IL2CPP::Il2CppObject *>(MyCoroutine().Get());
```

Available yield wrappers:

```cpp
BNM::Coroutine::AsyncOperation(intptr_t operation);
BNM::Coroutine::WaitForEndOfFrame();
BNM::Coroutine::WaitForFixedUpdate();
BNM::Coroutine::WaitForSeconds(float seconds);
BNM::Coroutine::WaitForSecondsRealtime(float seconds);
BNM::Coroutine::WaitUntil(std::function<bool()> function);
BNM::Coroutine::WaitWhile(std::function<bool()> function);
```

---

## 17. Utilities, Memory, and Safety

### Pointer checks

```cpp
if (BNM::CheckForNull(ptr)) {
    // ptr is not null
}

if (BNM::IsAllocated(ptr)) {
    // ptr appears to point to a valid allocated address
}
```

With `BNM_ALLOW_SAFE_IS_ALLOCATED`, `IsAllocated` uses a `/dev/null` write probe to avoid crashing on invalid pointers.

### Managed strings and extern methods

```cpp
auto *str = BNM::CreateMonoString("text");
void *icall = BNM::GetExternMethod("UnityEngine.GameObject::Find");
```

### Instance access helper templates

These optional helpers are useful in mod templates when you already have an object
instance and want to resolve a method, field, or property from that instance's
runtime class.

```cpp
template <typename T>
BNM::Method<T> GetMethod(BNM::IL2CPP::Il2CppObject *instant,
                         const std::string_view &name, int parameters = -1) {
    BNM::Method<T> method = BNM::Class(instant->klass).GetMethod(name, parameters);
    return method[instant];
}

template <typename T>
BNM::Method<T> GetMethod(void *instant, const std::string_view &name,
                         int parameters = -1) {
    return GetMethod<T>(static_cast<BNM::IL2CPP::Il2CppObject *>(instant), name, parameters);
}

template <typename T>
BNM::Method<T> GetMethod(
    BNM::IL2CPP::Il2CppObject *instant, const std::string_view &name,
    const std::initializer_list<std::string_view> &parameterNames) {
    BNM::Method<T> method = BNM::Class(instant->klass).GetMethod(name, parameterNames);
    return method[instant];
}

template <typename T>
BNM::Method<T> GetMethod(
    void *instant, const std::string_view &name,
    const std::initializer_list<std::string_view> &parameterNames) {
    return GetMethod<T>(static_cast<BNM::IL2CPP::Il2CppObject *>(instant), name, parameterNames);
}

template <typename T>
BNM::Method<T> GetMethod(
    BNM::IL2CPP::Il2CppObject *instant, const std::string_view &name,
    const std::initializer_list<BNM::CompileTimeClass> &parameterTypes) {
    BNM::Method<T> method = BNM::Class(instant->klass).GetMethod(name, parameterTypes);
    return method[instant];
}

template <typename T>
BNM::Method<T> GetMethod(
    void *instant, const std::string_view &name,
    const std::initializer_list<BNM::CompileTimeClass> &parameterTypes) {
    return GetMethod<T>(static_cast<BNM::IL2CPP::Il2CppObject *>(instant), name, parameterTypes);
}

template <typename T>
T GetField(BNM::IL2CPP::Il2CppObject *instant, const std::string_view &name) {
    BNM::Field<T> field = BNM::Class(instant->klass).GetField(name);
    return field[instant]();
}

template <typename T>
T GetProperty(BNM::IL2CPP::Il2CppObject *instant, const std::string_view &name) {
    BNM::Property<T> property = BNM::Class(instant->klass).GetProperty(name);
    return property[instant]();
}

template <typename T>
void SetProperty(BNM::IL2CPP::Il2CppObject *instant, const std::string_view &name,
                 T value) {
    BNM::Property<T> property = BNM::Class(instant->klass).GetProperty(name);
    property[instant]() = value;
}
```

### Boxing and unboxing

```cpp
auto boxed = BNM::Defaults::Box(123);
int *raw = BNM::UnboxObject(reinterpret_cast<int *>(boxed));
```

### IL2CPP GC allocation helpers

```cpp
void *mem = BNM::Allocate(size);
BNM::Free(mem);
```

### Unity 2023.2+/Unity 6 object unmarshalling

For `UNITY_VER >= 232`:

```cpp
auto ptr = BNM::UnmarshalUnityObject(gcHandlePtr);
```

---

## 18. Logging and Diagnostics

BNM uses `spdlog` and exposes log macros configured by `GlobalSettings.hpp`:

```cpp
BNM_LOG_INFO("Loaded %s", name);
BNM_LOG_DEBUG("Debug value: %d", value);
BNM_LOG_WARN("Warning: %s", reason);
BNM_LOG_ERR("Error: %s", reason);
```

Conditional forms exist for debug/error/warn depending on enabled macros:

```cpp
BNM_LOG_DEBUG_IF(condition, "debug message");
BNM_LOG_ERR_IF(condition, "error message");
BNM_LOG_WARN_IF(condition, "warning message");
```

The current logger captures source location (`__FILE__`, `__LINE__`, `__FUNCTION__`) through `BNM_LOG_SOURCE`.

---

## 19. Recommended Usage Patterns

### Always initialize through the loading lifecycle

Register mod logic with `BNM::Loading::AddOnLoadedEvent`. Avoid resolving game classes before BNM reports that IL2CPP is ready.

### Validate metadata handles

```cpp
auto klass = BNM::Class("", "PlayerController");
if (!klass) return;

auto method = klass.GetMethod("Update", 0);
if (!method.IsValid()) return;
```

### Attach non-Unity threads

Use `BNM::AttachIl2Cpp()`/`DetachIl2Cpp()` or `BNM_ATTACH_THREAD()` before calling IL2CPP APIs outside Unity-created threads.

### Check Unity object lifetime

For Unity objects, non-null is not enough. Use `Alive()` where available:

```cpp
if (object && object->Alive()) {
    // safe to use as a live UnityEngine.Object
}
```

### Prefer symbolic lookup, cache handles after load

Resolve by name after BNM is loaded, then cache `Class`, `MethodBase`, `FieldBase`, etc. when reused frequently.

### Pick the right hook type

- `BasicHook`: native inline hook through Dobby/ShadowHook/custom backend.
- `InvokeHook`: changes `MethodInfo`; best for invoke-path calls and Unity messages.
- `VirtualHook`: changes vtable entries for virtual dispatch on a target class.

### Match signatures exactly

IL2CPP method signatures vary for static/instance methods and older Unity versions. If parameters or return types do not match, hooks and typed calls can crash.

---

## 20. Minimal End-to-End Example

```cpp
#include <jni.h>
#include <BNM/Loading.hpp>
#include <BNM/Class.hpp>
#include <BNM/Method.hpp>
#include <BNM/Field.hpp>
#include <BNM/Defaults.hpp>

void (*old_Update)(BNM::IL2CPP::Il2CppObject *instance);

void my_Update(BNM::IL2CPP::Il2CppObject *instance) {
    static auto playerClass = BNM::Class("", "PlayerController");
    static auto healthField = playerClass.GetField("_health");

    if (healthField.IsValid()) {
        healthField[instance].SetValue<float>(999.0f);
    }

    if (old_Update) old_Update(instance);
}

void OnBNMLoaded() {
    auto playerClass = BNM::Class("", "PlayerController");
    auto update = playerClass.GetMethod("Update", 0);

    if (update.IsValid()) {
        BNM::BasicHook(update, my_Update, old_Update);
    }
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env{};
    vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);

    BNM::Loading::TryLoadByJNI(env);
    BNM::Loading::AddOnLoadedEvent(OnBNMLoaded);

    return JNI_VERSION_1_6;
}
```

---

## 21. Breaking Changes from Older Documentation

Older BNM documentation may be wrong for this codebase. The important updates are:

- **C++23 is required**, not C++20.
- Project version **`2.6.1-beta`**
- Hooking backend selection now has built-in `BNM_USE_DOBBY` and `BNM_USE_SHADOWHOOK` branches plus dummy fallback hooks.
- Unity 6 / `6000.x+` support is represented by `UNITY_VER 233`; current checked-in setting is `UNITY_VER 222`.
- Name-based `BNM::Class` construction requires namespace and class name, for example `BNM::Class("", "PlayerController")`.
- Modern method invocation uses `method[instance].Invoke<ReturnType>(args...)`.
- Modern field/property access uses `GetValue<T>()` and `SetValue<T>(value)`.
- `BNM::BasicHook` accepts `BNM::MethodBase` directly; you do not need to manually call `GetOffset()` for the normal BNM wrapper path.
- `BNM/Helpers.hpp` adds `BNM_CLASS`, `BNM_METHOD`, `BNM_FIELD`, `BNM_PROPERTY`, `BNM_HOOK`, `BNM_INVOKE_HOOK`, and `BNM_ATTACH_THREAD`.
- ClassesManagement includes additional method markers: `BNM_CustomMethodMarkAsInvokeHook`, `BNM_CustomMethodMarkAsBasicHook`, `BNM_CustomMethodSkipTypeMatch`, and `BNM_CustomMethodCopyAttributes`.
- Coroutine support is guarded by both `BNM_CLASSES_MANAGEMENT` and `BNM_COROUTINE`.

---

*Documentation updated for BNM Version `2.6.1-beta`, C++23*

