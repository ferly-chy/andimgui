#include <BNM/Field.hpp>
#include <BNM/Loading.hpp>
#include <BNM/UserSettings/GlobalSettings.hpp>
#include <BNM/UserSettings/Il2CppMethodNames.hpp>

#include "private/Logging.hpp"
#include <Internals.hpp>

using namespace BNM;

void Internal::Load() {
  SetupLogging();
  std::unique_lock lock(initMutex);
  if (states.loading || states.state)
    return;
  states.loading = true;

  std::shared_lock loadingLock(loadingMutex);

  // Load BNM
  if (!SetupBNM()) {
    states.loading = false;
    return;
  }

  BNM::Internal::LoadDefaults();

#ifdef BNM_CLASSES_MANAGEMENT

#ifdef BNM_COROUTINE
  BNM::Internal::SetupCoroutine();
#endif

  BNM::Internal::ClassesManagement::ProcessCustomClasses();

#ifdef BNM_COROUTINE
  BNM::Internal::LoadCoroutine();
#endif

#endif
  states.state = true;
  states.loading = false;

  // Call all events after loading il2cpp
  BNM::ForwardList<void (*)()> events;
  {
    std::lock_guard eventsLock(onLoadedEventsMutex);
    events = onIl2CppLoaded;
  }
  if (!events.IsEmpty()) {
    auto current = events.lastElement->next;
    do {
      current->value();

      current = current->next;
    } while (current != events.lastElement->next);
  }
}

void *Internal::GetIl2CppMethod(const char *methodName) {
  return currentFinderMethod(methodName, currentFinderData);
}

void Loading::AllowLateInitHook() { Internal::states.lateInitAllowed = true; }

static bool CheckHandle(void *handle) {
  void *init = BNM_dlsym(handle, BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_init));
  if (!init)
    return false;

  Internal::BNM_il2cpp_init_origin = ::BasicHook(
      init, Internal::BNM_il2cpp_init, Internal::old_BNM_il2cpp_init);
  if (!Internal::BNM_il2cpp_init_origin || !Internal::old_BNM_il2cpp_init) {
    BNM_LOG_ERR("BNM: Failed to hook il2cpp_init. Cannot proceed.");
    return false;
  }

  if (Internal::states.lateInitAllowed)
    Internal::LateInit(BNM_dlsym(
        handle,
        BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_class_from_il2cpp_type)));

  Internal::il2cppLibraryHandle = handle;
  return true;
}

#if defined(__ARM_ARCH_7A__)
#define CURRENT_ARCH "armeabi-v7a"
#define CURRENT_ARCH_SPLIT "armeabi_v7a"
#elif defined(__aarch64__)
#define CURRENT_ARCH "arm64-v8a"
#define CURRENT_ARCH_SPLIT "arm64_v8a"
#elif defined(__i386__)
#define CURRENT_ARCH "x86"
#define CURRENT_ARCH_SPLIT "x86"
#elif defined(__x86_64__)
#define CURRENT_ARCH "x86_64"
#define CURRENT_ARCH_SPLIT "x86_64"
#elif defined(__riscv)
#define CURRENT_ARCH "riscv64"
#define CURRENT_ARCH_SPLIT "riscv64"
#endif

bool Loading::TryLoadByJNI(JNIEnv *env, jobject context) {
  Internal::SetupLogging();
  bool result = false;
  bool deleteContextLocalRef = false;

  if (!env || Internal::il2cppLibraryHandle || Internal::states.state)
    return result;

  if (context == nullptr) {
    jclass activityThread =
        env->FindClass(BNM_OBFUSCATE_TMP("android/app/ActivityThread"));
    if (env->ExceptionCheck() || !activityThread) {
      env->ExceptionClear();
      return false;
    }

    jmethodID currentActivityThreadMethod = env->GetStaticMethodID(
        activityThread, BNM_OBFUSCATE_TMP("currentActivityThread"),
        BNM_OBFUSCATE_TMP("()Landroid/app/ActivityThread;"));
    if (env->ExceptionCheck() || !currentActivityThreadMethod) {
      env->ExceptionClear();
      env->DeleteLocalRef(activityThread);
      return false;
    }

    auto currentActivityThread = env->CallStaticObjectMethod(
        activityThread, currentActivityThreadMethod);
    if (env->ExceptionCheck() || !currentActivityThread) {
      env->ExceptionClear();
      env->DeleteLocalRef(activityThread);
      return false;
    }

    jmethodID getApplicationMethod =
        env->GetMethodID(activityThread, BNM_OBFUSCATE_TMP("getApplication"),
                         BNM_OBFUSCATE_TMP("()Landroid/app/Application;"));
    if (env->ExceptionCheck() || !getApplicationMethod) {
      env->ExceptionClear();
      env->DeleteLocalRef(currentActivityThread);
      env->DeleteLocalRef(activityThread);
      return false;
    }

    context =
        env->CallObjectMethod(currentActivityThread, getApplicationMethod);
    env->DeleteLocalRef(currentActivityThread);
    env->DeleteLocalRef(activityThread);
    if (env->ExceptionCheck() || !context) {
      env->ExceptionClear();
      return false;
    }
    deleteContextLocalRef = true;
  }

  jclass contextClass = env->GetObjectClass(context);
  jmethodID getApplicationInfoMethod = env->GetMethodID(
      contextClass, BNM_OBFUSCATE_TMP("getApplicationInfo"),
      BNM_OBFUSCATE_TMP("()Landroid/content/pm/ApplicationInfo;"));
  if (env->ExceptionCheck() || !getApplicationInfoMethod) {
    env->ExceptionClear();
    env->DeleteLocalRef(contextClass);
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }

  auto applicationInfo =
      env->CallObjectMethod(context, getApplicationInfoMethod);
  env->DeleteLocalRef(contextClass);
  if (env->ExceptionCheck() || !applicationInfo) {
    env->ExceptionClear();
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }

  auto applicationInfoClass = env->GetObjectClass(applicationInfo);
  jfieldID flagsField = env->GetFieldID(
      applicationInfoClass, BNM_OBFUSCATE_TMP("flags"), BNM_OBFUSCATE_TMP("I"));
  if (env->ExceptionCheck() || !flagsField) {
    env->ExceptionClear();
    env->DeleteLocalRef(applicationInfo);
    env->DeleteLocalRef(applicationInfoClass);
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }

  auto flags = env->GetIntField(applicationInfo, flagsField);
  bool isLibrariesExtracted =
      (flags & 0x10000000) ==
      0x10000000; // ApplicationInfo.FLAG_EXTRACT_NATIVE_LIBS

  jfieldID dirField = env->GetFieldID(
      applicationInfoClass,
      isLibrariesExtracted ? BNM_OBFUSCATE_TMP("nativeLibraryDir")
                           : BNM_OBFUSCATE_TMP("sourceDir"),
      BNM_OBFUSCATE_TMP("Ljava/lang/String;"));
  if (env->ExceptionCheck() || !dirField) {
    env->ExceptionClear();
    env->DeleteLocalRef(applicationInfo);
    env->DeleteLocalRef(applicationInfoClass);
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }

  auto jDir = (jstring)env->GetObjectField(applicationInfo, dirField);
  if (env->ExceptionCheck() || !jDir) {
    env->ExceptionClear();
    env->DeleteLocalRef(applicationInfo);
    env->DeleteLocalRef(applicationInfoClass);
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }

  auto cDirChars = env->GetStringUTFChars(jDir, nullptr);
  if (!cDirChars) {
    env->DeleteLocalRef(applicationInfo);
    env->DeleteLocalRef(applicationInfoClass);
    env->DeleteLocalRef(jDir);
    if (deleteContextLocalRef)
      env->DeleteLocalRef(context);
    return false;
  }
  auto cDir = std::string_view(cDirChars);
  env->DeleteLocalRef(applicationInfo);
  env->DeleteLocalRef(applicationInfoClass);

  // isLibrariesExtracted = true:   Full path to library /data/app/.../package
  // name-.../lib/architecture/libil2cpp.so isLibrariesExtracted = false:  Full
  // path to library in base.apk /data/app/.../package
  // name-.../base.apk!/lib/architecture/libil2cpp.so
  std::string file =
      std::string(cDir) +
      (isLibrariesExtracted
           ? BNM_OBFUSCATE_TMP("/libil2cpp.so")
           : BNM_OBFUSCATE_TMP("!/lib/" CURRENT_ARCH "/libil2cpp.so"));

  // Try to load il2cpp using this path
  auto handle = BNM_dlopen(file.c_str(), RTLD_LAZY);
  if (!(result = CheckHandle(handle))) {
    BNM_LOG_ERR_IF(isLibrariesExtracted, DBG_BNM_MSG_TryLoadByJNI_Fail);
  } else
    goto FINISH;
  if (isLibrariesExtracted)
    goto FINISH;
  file.clear();

  // Full path to library in split_config.architecture.apk /data/app/.../package
  // name-.../split_config.architecture.apk!/lib/architecture/libil2cpp.so
  file = std::string(cDir).substr(0, cDir.length() - 8) +
         BNM_OBFUSCATE_TMP("split_config." CURRENT_ARCH_SPLIT
                           ".apk!/lib/" CURRENT_ARCH "/libil2cpp.so");
  handle = BNM_dlopen(file.c_str(), RTLD_LAZY);
  if (!(result = CheckHandle(handle)))
    BNM_LOG_ERR(DBG_BNM_MSG_TryLoadByJNI_Fail);

FINISH:
  env->ReleaseStringUTFChars(jDir, cDirChars);
  env->DeleteLocalRef(jDir);
  if (deleteContextLocalRef)
    env->DeleteLocalRef(context);
  return result;
}

bool Loading::TryLoadByDlfcnHandle(void *handle) {
  Internal::SetupLogging();
  return CheckHandle(handle);
}

void Loading::SetMethodFinder(BNM::Loading::MethodFinder finderMethod,
                              void *userData) {
  Internal::currentFinderMethod = finderMethod;
  Internal::currentFinderData = userData;
}

bool Loading::TryLoadByUsersFinder() {
  Internal::SetupLogging();

  auto init = Internal::currentFinderMethod(
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_init),
      Internal::currentFinderData);
  if (!init)
    return false;

  Internal::BNM_il2cpp_init_origin = ::BasicHook(
      init, Internal::BNM_il2cpp_init, Internal::old_BNM_il2cpp_init);
  if (!Internal::BNM_il2cpp_init_origin || !Internal::old_BNM_il2cpp_init) {
    BNM_LOG_ERR("BNM: Failed to hook il2cpp_init. Cannot proceed.");
    return false;
  }

  if (Internal::states.lateInitAllowed)
    Internal::LateInit(Internal::currentFinderMethod(
        BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_class_from_il2cpp_type),
        Internal::currentFinderData));

  return true;
}

void Loading::TrySetupByUsersFinder() { return Internal::Load(); }

#include "BNM/AssemblerUtils.hpp"

void Internal::LateInit(void *il2cpp_class_from_il2cpp_type_addr) {
  if (!il2cpp_class_from_il2cpp_type_addr)
    return;

#if defined(__ARM_ARCH_7A__) || defined(__aarch64__)
  const uint8_t count = 1;
#elif defined(__i386__) || defined(__x86_64__)
  // x86 has one add-on call at start
  const uint8_t count = 2;
#endif

  //! il2cpp::vm::Class::FromIl2CppType
  // Путь (Path):
  // il2cpp_class_from_il2cpp_type ->
  // il2cpp::vm::Class::FromIl2CppType
  auto from_il2cpp_type = AssemblerUtils::FindNextJump(
      (BNM_PTR)il2cpp_class_from_il2cpp_type_addr, count);

  if (!from_il2cpp_type || !IsAllocated((void *)from_il2cpp_type)) {
    BNM_LOG_WARN("BNM: Failed to resolve valid Class::FromIl2CppType hook "
                 "target for late init (%p).",
                 (void *)from_il2cpp_type);
    return;
  }

  Internal::BNM_Class$$FromIl2CppType_origin =
      ::BasicHook(from_il2cpp_type, Internal::BNM_Class$$FromIl2CppType,
                  Internal::old_BNM_Class$$FromIl2CppType);
  if (!Internal::BNM_Class$$FromIl2CppType_origin ||
      !Internal::old_BNM_Class$$FromIl2CppType)
    BNM_LOG_WARN("BNM: Failed to install late init Class::FromIl2CppType hook.");
}

static void EmptyMethod() {}

#ifdef BNM_DEBUG
static void *OffsetInLib(void *offsetInMemory) {
  if (offsetInMemory == nullptr)
    return nullptr;
  Dl_info info;
  BNM_dladdr(offsetInMemory, &info);
  return (void *)((BNM_PTR)offsetInMemory - (BNM_PTR)info.dli_fbase);
}

void *Utils::OffsetInLib(void *offsetInMemory) {
  return ::OffsetInLib(offsetInMemory);
}
#endif

bool Internal::SetupBNM() {
#if defined(__ARM_ARCH_7A__) || defined(__aarch64__)
  const uint8_t count = 1;
#elif defined(__i386__) || defined(__x86_64__)
  // x86 has one add-on call at start
  const uint8_t count = 2;
#endif

  // Helper for nested jump resolution
  auto ResolveNestedJump = [](BNM_PTR start, uint8_t depth,
                              uint8_t jumpIndex) -> BNM_PTR {
    BNM_PTR current = start;
    for (uint8_t i = 0; i < depth; ++i) {
      current = AssemblerUtils::FindNextJump(current, jumpIndex);
      if (!current)
        return 0;
    }
    return current;
  };

  auto IsValidResolvedAddress = [](BNM_PTR address) -> bool {
    return address && IsAllocated((void *)address);
  };

  //! il2cpp::vm::Class::Init
  auto array_new_specific = (BNM_PTR)GetIl2CppMethod(
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_array_new_specific));
  auto array_new = (BNM_PTR)GetIl2CppMethod(
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_array_new));
  auto value_box = (BNM_PTR)GetIl2CppMethod(
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_value_box));

  if (!array_new_specific || !array_new || !value_box) {
    BNM_LOG_ERR("BNM: Critical API missing. Cannot proceed.");
    return false;
  }

  // Consensus Search with CORRECT nesting
  // Path 1 & 2: il2cpp_array_... -> Array::... -> Class::Init
  // Path 3: il2cpp_value_box -> Class::Init
  BNM_PTR p1 = ResolveNestedJump(array_new_specific, 2, count);
  BNM_PTR p2 = ResolveNestedJump(array_new, 2, count);
  BNM_PTR p3 = ResolveNestedJump(value_box, 1, count);

  if (p1 && (p1 == p2 || p1 == p3))
    il2cppMethods.Class$$Init = (decltype(il2cppMethods.Class$$Init))p1;
  else if (p2 && p2 == p3)
    il2cppMethods.Class$$Init = (decltype(il2cppMethods.Class$$Init))p2;
  else {
    // Fallback to the most historical stable path if consensus fails
    BNM_LOG_WARN("BNM: Class::Init consensus failed (P1:%p, P2:%p, P3:%p). "
                 "Using Path 1 fallback.",
                 (void *)p1, (void *)p2, (void *)p3);
    il2cppMethods.Class$$Init = (decltype(il2cppMethods.Class$$Init))p1;
  }

  // Sanity check: Ensure Class::Init is actually resolved and looks like a
  // valid pointer
  if (!il2cppMethods.Class$$Init ||
      !IsAllocated((void *)il2cppMethods.Class$$Init)) {
    BNM_LOG_ERR("BNM: Failed to resolve Class::Init or address is invalid. "
                "Modding will fail.");
    return false;
  }

  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Class_Init,
                OffsetInLib((void *)il2cppMethods.Class$$Init));

#define INIT_IL2CPP_API(name)                                                  \
  il2cppMethods.name = (decltype(il2cppMethods.name))GetIl2CppMethod(          \
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_##name));                               \
  BNM_LOG_DEBUG("[INIT_IL2CPP_API]: " #name " (" BNM_IL2CPP_API_##name         \
                ") in lib %p",                                                 \
                OffsetInLib((void *)il2cppMethods.name))

  INIT_IL2CPP_API(il2cpp_image_get_class);
  INIT_IL2CPP_API(il2cpp_get_corlib);
  INIT_IL2CPP_API(il2cpp_class_from_name);
  INIT_IL2CPP_API(il2cpp_assembly_get_image);
  INIT_IL2CPP_API(il2cpp_method_get_param_name);
  INIT_IL2CPP_API(il2cpp_class_from_il2cpp_type);
  INIT_IL2CPP_API(il2cpp_array_class_get);
  INIT_IL2CPP_API(il2cpp_type_get_object);
  INIT_IL2CPP_API(il2cpp_object_new);
  INIT_IL2CPP_API(il2cpp_value_box);
  INIT_IL2CPP_API(il2cpp_array_new);
  INIT_IL2CPP_API(il2cpp_field_static_get_value);
  INIT_IL2CPP_API(il2cpp_field_static_set_value);
  INIT_IL2CPP_API(il2cpp_string_new);
  INIT_IL2CPP_API(il2cpp_resolve_icall);
  INIT_IL2CPP_API(il2cpp_runtime_invoke);
  INIT_IL2CPP_API(il2cpp_domain_get);
  INIT_IL2CPP_API(il2cpp_thread_current);
  INIT_IL2CPP_API(il2cpp_thread_attach);
  INIT_IL2CPP_API(il2cpp_thread_detach);
  INIT_IL2CPP_API(il2cpp_gc_alloc_fixed);
  INIT_IL2CPP_API(il2cpp_gc_free_fixed);

#undef INIT_IL2CPP_API

  //! il2cpp::vm::Image::GetTypes
  if (il2cppMethods.il2cpp_image_get_class == nullptr) {
    if (!il2cppMethods.il2cpp_class_from_name ||
        !il2cppMethods.il2cpp_get_corlib) {
      BNM_LOG_ERR("BNM: Failed to resolve Image::GetTypes fallback: missing "
                  "il2cpp_class_from_name or il2cpp_get_corlib.");
      return false;
    }

    auto corlib = il2cppMethods.il2cpp_get_corlib();
    if (!corlib) {
      BNM_LOG_ERR("BNM: Failed to resolve Image::GetTypes fallback: corlib is "
                  "null.");
      return false;
    }

    auto assemblyClass = il2cppMethods.il2cpp_class_from_name(
        corlib, BNM_OBFUSCATE_TMP("System.Reflection"),
        BNM_OBFUSCATE_TMP("Assembly"));
    BNM_PTR GetTypesAdr = Class(assemblyClass)
                              .GetMethod(BNM_OBFUSCATE_TMP("GetTypes"), 1)
                              .GetOffset();

#if UNITY_VER >= 211
    const int sCount = count;
#elif UNITY_VER > 174
    const int sCount = count + 1;
#else
    const int sCount = count + 2;
#endif
    // Path:
    // System.Reflection.Assembly.GetTypes(bool) ->
    // il2cpp::icalls::mscorlib::System::Reflection::Assembly::GetTypes ->
    // il2cpp::icalls::mscorlib::System::Module::InternalGetTypes ->
    // il2cpp::vm::Image::GetTypes
    auto image_get_types = ResolveNestedJump(GetTypesAdr, 1, count);
    image_get_types = ResolveNestedJump(image_get_types, 1, sCount);
    image_get_types = ResolveNestedJump(image_get_types, 1, count);
    if (!IsValidResolvedAddress(image_get_types)) {
      BNM_LOG_ERR("BNM: Failed to resolve valid Image::GetTypes address "
                  "(%p). Cannot proceed.",
                  (void *)image_get_types);
      return false;
    }
    il2cppMethods.orig_Image$$GetTypes =
        (decltype(il2cppMethods.orig_Image$$GetTypes))image_get_types;

    BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Image_GetTypes,
                  OffsetInLib((void *)il2cppMethods.orig_Image$$GetTypes));
  } else
    BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_image_get_class_exists);

#ifdef BNM_CLASSES_MANAGEMENT

  //! il2cpp::vm::Class::FromIl2CppType
  // Path:
  // il2cpp_class_from_type ->
  // il2cpp::vm::Class::FromIl2CppType
  auto from_type_adr =
      AssemblerUtils::FindNextJump((BNM_PTR)GetIl2CppMethod(BNM_OBFUSCATE_TMP(
                                       BNM_IL2CPP_API_il2cpp_class_from_type)),
                                   count);
  if (!IsValidResolvedAddress(from_type_adr)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid Class::FromIl2CppType hook "
                "target (%p). Cannot proceed.",
                (void *)from_type_adr);
    return false;
  }
  auto fromTypeHook = ::BasicHook(
      from_type_adr, ClassesManagement::Class$$FromIl2CppType,
      ClassesManagement::old_Class$$FromIl2CppType);
  if (!fromTypeHook || !ClassesManagement::old_Class$$FromIl2CppType) {
    BNM_LOG_ERR("BNM: Failed to hook Class::FromIl2CppType. Cannot proceed.");
    return false;
  }
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Class_FromIl2CppType,
                OffsetInLib((void *)from_type_adr));

  //! il2cpp::vm::Type::GetClassOrElementClass
  // Path:
  // il2cpp_type_get_class_or_element_class ->
  // il2cpp::vm::Type::GetClassOrElementClass
  auto type_get_class_adr = AssemblerUtils::FindNextJump(
      (BNM_PTR)GetIl2CppMethod(BNM_OBFUSCATE_TMP(
          BNM_IL2CPP_API_il2cpp_type_get_class_or_element_class)),
      count);
  if (!IsValidResolvedAddress(type_get_class_adr)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid Type::GetClassOrElementClass "
                "hook target (%p). Cannot proceed.",
                (void *)type_get_class_adr);
    return false;
  }
  auto typeGetClassHook = ::BasicHook(
      type_get_class_adr, ClassesManagement::Type$$GetClassOrElementClass,
      ClassesManagement::old_Type$$GetClassOrElementClass);
  if (!typeGetClassHook || !ClassesManagement::old_Type$$GetClassOrElementClass) {
    BNM_LOG_ERR("BNM: Failed to hook Type::GetClassOrElementClass. Cannot "
                "proceed.");
    return false;
  }
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Type_GetClassOrElementClass,
                OffsetInLib((void *)type_get_class_adr));

  //! il2cpp::vm::Image::ClassFromName
  // Path:
  // il2cpp_class_from_name ->
  // il2cpp::vm::Class::FromName ->
  // il2cpp::vm::Image::ClassFromName
  auto from_name_adr = AssemblerUtils::FindNextJump(
      AssemblerUtils::FindNextJump(
          (BNM_PTR)il2cppMethods.il2cpp_class_from_name, count),
      count);
  if (!IsValidResolvedAddress(from_name_adr)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid Image::ClassFromName hook "
                "target (%p). Cannot proceed.",
                (void *)from_name_adr);
    return false;
  }
  auto fromNameHook = ::BasicHook(from_name_adr, ClassesManagement::Class$$FromName,
                                  ClassesManagement::old_Class$$FromName);
  if (!fromNameHook || !ClassesManagement::old_Class$$FromName) {
    BNM_LOG_ERR("BNM: Failed to hook Image::ClassFromName. Cannot proceed.");
    return false;
  }
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Image_FromName,
                OffsetInLib((void *)from_name_adr));
#if UNITY_VER <= 174

  //! il2cpp::vm::MetadataCache::GetImageFromIndex
  // Path:
  // il2cpp_assembly_get_image ->
  // il2cpp::vm::Assembly::GetImage ->
  // il2cpp::vm::MetadataCache::GetImageFromIndex
  auto GetImageFromIndexOffset = AssemblerUtils::FindNextJump(
      AssemblerUtils::FindNextJump(
          (BNM_PTR)il2cppMethods.il2cpp_assembly_get_image, count),
      count);
  if (!IsValidResolvedAddress(GetImageFromIndexOffset)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid MetadataCache::GetImageFromIndex "
                "hook target (%p). Cannot proceed.",
                (void *)GetImageFromIndexOffset);
    return false;
  }
  auto getImageFromIndexHook = ::BasicHook(
      GetImageFromIndexOffset, ClassesManagement::new_GetImageFromIndex,
      ClassesManagement::old_GetImageFromIndex);
  if (!getImageFromIndexHook || !ClassesManagement::old_GetImageFromIndex) {
    BNM_LOG_ERR("BNM: Failed to hook MetadataCache::GetImageFromIndex. Cannot "
                "proceed.");
    return false;
  }
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_MetadataCache_GetImageFromIndex,
                OffsetInLib((void *)GetImageFromIndexOffset));

  //! il2cpp::vm::Assembly::Load
  // Path:
  // il2cpp_domain_assembly_open ->
  // il2cpp::vm::Assembly::Load
  BNM_PTR AssemblyLoadOffset = AssemblerUtils::FindNextJump(
      (BNM_PTR)BNM_dlsym(
          il2cppLibraryHandle,
          BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_domain_assembly_open)),
      count);
  if (!IsValidResolvedAddress(AssemblyLoadOffset)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid Assembly::Load hook target "
                "(%p). Cannot proceed.",
                (void *)AssemblyLoadOffset);
    return false;
  }
  auto assemblyLoadHook =
      ::BasicHook(AssemblyLoadOffset, ClassesManagement::Assembly$$Load, nullptr);
  if (!assemblyLoadHook)
    BNM_LOG_WARN("BNM: Failed to hook Assembly::Load. Continuing without load "
                 "notification hook.");
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Assembly_Load,
                OffsetInLib((void *)AssemblyLoadOffset));

#endif
#endif

  //! il2cpp::vm::Assembly::GetAllAssemblies
  // Path:
  // il2cpp_domain_get_assemblies ->
  // il2cpp::vm::Assembly::GetAllAssemblies
  auto adr = (BNM_PTR)GetIl2CppMethod(
      BNM_OBFUSCATE_TMP(BNM_IL2CPP_API_il2cpp_domain_get_assemblies));
  auto get_all_assemblies = AssemblerUtils::FindNextJump(adr, count);
  if (!IsValidResolvedAddress(get_all_assemblies)) {
    BNM_LOG_ERR("BNM: Failed to resolve valid Assembly::GetAllAssemblies "
                "address (%p). Cannot proceed.",
                (void *)get_all_assemblies);
    return false;
  }
  il2cppMethods.Assembly$$GetAllAssemblies =
      (std::vector<IL2CPP::Il2CppAssembly *> * (*)()) get_all_assemblies;
  BNM_LOG_DEBUG(DBG_BNM_MSG_SetupBNM_Assembly_GetAllAssemblies,
                OffsetInLib((void *)il2cppMethods.Assembly$$GetAllAssemblies));

  auto mscorlib = il2cppMethods.il2cpp_get_corlib();

  // Get MakeGenericMethod_impl. Depending on the version of Unity, it may be in
  // different classes.
  auto runtimeMethodInfoClassPtr =
      TryGetClassInImage(mscorlib, BNM_OBFUSCATE_TMP("System.Reflection"),
                         BNM_OBFUSCATE_TMP("RuntimeMethodInfo"));
  if (runtimeMethodInfoClassPtr) {
    Internal::il2cppMethods.Class$$Init(runtimeMethodInfoClassPtr);
    vmData.RuntimeMethodInfo$$MakeGenericMethod_impl =
        BNM::MethodBase(IterateMethods(
            runtimeMethodInfoClassPtr,
            [methodName = BNM_OBFUSCATE_TMP("MakeGenericMethod_impl")](
                const MethodBase &methodBase) {
              return !strcmp(methodBase._data->name, methodName);
            }));
  }
  if (!vmData.RuntimeMethodInfo$$MakeGenericMethod_impl.IsValid())
    vmData.RuntimeMethodInfo$$MakeGenericMethod_impl =
        Class(BNM_OBFUSCATE_TMP("System.Reflection"),
              BNM_OBFUSCATE_TMP("MonoMethod"), mscorlib)
            .GetMethod(BNM_OBFUSCATE_TMP("MakeGenericMethod_impl"));

  auto runtimeTypeClass = Class(BNM_OBFUSCATE_TMP("System"),
                                BNM_OBFUSCATE_TMP("RuntimeType"), mscorlib);
  auto stringClass =
      Class(BNM_OBFUSCATE_TMP("System"), BNM_OBFUSCATE_TMP("String"), mscorlib);
  auto interlockedClass = Class(BNM_OBFUSCATE_TMP("System.Threading"),
                                BNM_OBFUSCATE_TMP("Interlocked"), mscorlib);
  auto objectClass =
      Class(BNM_OBFUSCATE_TMP("System"), BNM_OBFUSCATE_TMP("Object"), mscorlib);
  if (!objectClass._data) {
    BNM_LOG_ERR("BNM: Failed to find System.Object. CRITICAL ERROR.");
    return false;
  }
  for (uint16_t slot = 0; slot < objectClass._data->vtable_count; slot++) {
    const BNM::IL2CPP::MethodInfo *vMethod =
        objectClass._data->vtable[slot].method;
    if (!vMethod || !vMethod->name ||
        strcmp(vMethod->name, BNM_OBFUSCATE_TMP("Finalize")) != 0)
      continue;
    finalizerSlot = slot;
    break;
  }

  auto UnityEngineCoreModule =
      Image(BNM_OBFUSCATE_TMP("UnityEngine.CoreModule.dll"));

  vmData.Object = objectClass;
  vmData.UnityEngine$$Object =
      Class(BNM_OBFUSCATE_TMP("UnityEngine"), BNM_OBFUSCATE_TMP("Object"),
            UnityEngineCoreModule);
  vmData.Type$$GetType =
      Class(BNM_OBFUSCATE_TMP("System"), BNM_OBFUSCATE_TMP("Type"), mscorlib)
          .GetMethod(BNM_OBFUSCATE_TMP("GetType"), 1);
  vmData.Interlocked$$CompareExchange =
      interlockedClass.GetMethod(BNM_OBFUSCATE_TMP("CompareExchange"),
                                 {objectClass, objectClass, objectClass});
  vmData.RuntimeType$$MakeGenericType =
      runtimeTypeClass.GetMethod(BNM_OBFUSCATE_TMP("MakeGenericType"), 2);
  vmData.RuntimeType$$MakePointerType =
      runtimeTypeClass.GetMethod(BNM_OBFUSCATE_TMP("MakePointerType"), 1);
  vmData.RuntimeType$$make_byref_type =
      runtimeTypeClass.GetMethod(BNM_OBFUSCATE_TMP("make_byref_type"), 0);
  vmData.String$$Empty = stringClass.GetField(BNM_OBFUSCATE_TMP("Empty"))
                             .cast<Structures::Mono::String *>()
                             .GetPointer();

  auto listClass = vmData.System$$List =
      Class(BNM_OBFUSCATE_TMP("System.Collections.Generic"),
            BNM_OBFUSCATE_TMP("List`1"));
  if (!listClass._data) {
    BNM_LOG_ERR("BNM: Failed to find System.Collections.Generic.List`1. "
                "Modding might be limited.");
    return true;
  }
  auto cls = listClass._data;
  auto size = sizeof(IL2CPP::Il2CppClass) +
              cls->vtable_count * sizeof(IL2CPP::VirtualInvokeData);
  listClass._data = (IL2CPP::Il2CppClass *)BNM_malloc(size);
  memcpy(listClass._data, cls, size);
  listClass._data->has_finalize = 0;
  listClass._data->instance_size = sizeof(Structures::Mono::List<void *>);

  // Bypassing the creation of a static _emptyArray field because it cannot
  // exist
  listClass._data->has_cctor = 0;
  listClass._data->cctor_started = 0;
#if UNITY_VER >= 212
  listClass._data->cctor_finished_or_no_cctor = 1;
#else
  listClass._data->cctor_finished = 1;
#endif

  auto constructor = listClass.GetMethod(Internal::constructorName, 0)._data;

  auto newMethods = (IL2CPP::MethodInfo **)BNM_malloc(
      sizeof(IL2CPP::MethodInfo *) * listClass._data->method_count);
  memcpy(newMethods, listClass._data->methods,
         sizeof(IL2CPP::MethodInfo *) * listClass._data->method_count);

  auto newConstructor =
      (IL2CPP::MethodInfo *)BNM_malloc(sizeof(IL2CPP::MethodInfo));
  *newConstructor = *constructor;
  newConstructor->methodPointer =
      (decltype(newConstructor->methodPointer))EmptyMethod;
  newConstructor->invoker_method =
      (decltype(newConstructor->invoker_method))EmptyMethod;

  for (uint16_t i = 0; i < listClass._data->method_count; ++i) {
    if (listClass._data->methods[i] == constructor) {
      newMethods[i] = newConstructor;
      continue;
    }
    newMethods[i] = (IL2CPP::MethodInfo *)listClass._data->methods[i];
  }
  listClass._data->methods = (const IL2CPP::MethodInfo **)newMethods;
  customListTemplateClass = listClass;

  return true;
}

void Loading::AddOnLoadedEvent(void (*event)()) {
  if (!event)
    return;
  std::lock_guard lock(Internal::onLoadedEventsMutex);
  Internal::onIl2CppLoaded.Add(event);
}

void Loading::ClearOnLoadedEvents() {
  std::lock_guard lock(Internal::onLoadedEventsMutex);
  Internal::onIl2CppLoaded.Clear();
}
