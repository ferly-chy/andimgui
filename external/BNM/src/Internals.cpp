#include <Internals.hpp>

namespace BNM::Internal {
States states{};
void *il2cppLibraryHandle{};

void *BasicFinder(const char *name, void *userData) {
  return BNM_dlsym(*(void **)userData, name);
}

Loading::MethodFinder currentFinderMethod = BasicFinder;
void *currentFinderData = &il2cppLibraryHandle;

// A list with variables from the il2cpp VM
VMData vmData{};

// il2cpp methods to avoid searching for them every BNM query
Il2CppMethods il2cppMethods{};

BNM::ForwardList<void (*)()> onIl2CppLoaded{};

std::string_view constructorName = BNM_OBFUSCATE(".ctor");
BNM::Class customListTemplateClass{};
std::map<uint32_t, BNM::Class> customListsMap{};
int32_t finalizerSlot = -1;

ImageCacheMap imageCache{};
ClassCacheMap classCache{};
MethodCacheMap methodCache{};
GlobalClassCacheMap globalClassCache{};

#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
std::shared_mutex imageCacheMutex{};
std::shared_mutex classCacheMutex{};
std::shared_mutex methodCacheMutex{};
std::shared_mutex globalClassCacheMutex{};
#endif

void *bnmImageSentinel = (void *)&bnmImageSentinel;

void *BNM_il2cpp_init_origin{};
int (*old_BNM_il2cpp_init)(const char *){};

void *BNM_Class$$FromIl2CppType_origin{};
IL2CPP::Il2CppClass *(*old_BNM_Class$$FromIl2CppType)(
    IL2CPP::Il2CppReflectionType *){};

#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
std::shared_mutex loadingMutex{};
std::recursive_mutex initMutex{};
#endif

#ifdef BNM_CLASSES_MANAGEMENT
namespace ClassesManagement {
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
std::shared_mutex classesFindAccessMutex{};
#endif
std::vector<MANAGEMENT_STRUCTURES::CustomClass *> &
GetClassesManagementVector() {
  static std::vector<MANAGEMENT_STRUCTURES::CustomClass *> vector;
  return vector;
}

IL2CPP::Il2CppClass *(*old_Class$$FromIl2CppType)(IL2CPP::Il2CppType *type){};

IL2CPP::Il2CppClass *(*old_Type$$GetClassOrElementClass)(
    IL2CPP::Il2CppType *type){};

IL2CPP::Il2CppClass *(*old_Class$$FromName)(IL2CPP::Il2CppImage *image,
                                            const char *ns, const char *name){};

#if UNITY_VER <= 174
IL2CPP::Il2CppImage *(*old_GetImageFromIndex)(IL2CPP::ImageIndex index){};
#endif
BNMClassesMap bnmClassesMap{};

// Track allocations made with BNM_malloc during runtime class creation
std::vector<void *> allocatedMemory{};
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
std::mutex memoryTrackerMutex{};
#endif
void TrackAllocation(void *ptr) {
  if (!ptr)
    return;
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
  std::lock_guard lock(memoryTrackerMutex);
#endif
  allocatedMemory.push_back(ptr);
}

void FreeAllAllocations() {
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
  std::lock_guard lock(memoryTrackerMutex);
#endif
  for (auto ptr : allocatedMemory)
    BNM_free(ptr);
  allocatedMemory.clear();
}
} // namespace ClassesManagement
#endif
} // namespace BNM::Internal

using namespace BNM;

IL2CPP::Il2CppImage *Internal::TryGetImage(const std::string_view &_name) {
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
  std::shared_lock lock(Internal::imageCacheMutex);
#endif
  if (auto it = Internal::imageCache.find(_name);
      it != Internal::imageCache.end())
    return it->second;

#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
  lock.unlock();
#endif

  auto &assemblies = *Internal::il2cppMethods.Assembly$$GetAllAssemblies();

  for (auto assembly : assemblies) {
    auto currentImage =
        Internal::il2cppMethods.il2cpp_assembly_get_image(assembly);
    if (!Internal::CompareImageName(currentImage, _name))
      continue;

#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
    std::unique_lock writeLock(Internal::imageCacheMutex);
#endif
    Internal::imageCache[std::string(_name)] = currentImage;
    return currentImage;
  }

  return {};
}

IL2CPP::Il2CppClass *
Internal::TryGetClassInImage(const IL2CPP::Il2CppImage *image,
                             const std::string_view &_namespace,
                             const std::string_view &_name) {
  if (!image)
    return nullptr;

  char fullNameBuf[_namespace.size() + _name.size() + 3];
  size_t len = 0;
  if (!_namespace.empty()) {
    memcpy(fullNameBuf, _namespace.data(), _namespace.size());
    len += _namespace.size();
    fullNameBuf[len++] = ':';
    fullNameBuf[len++] = ':';
  }
  memcpy(fullNameBuf + len, _name.data(), _name.size());
  len += _name.size();
  std::string_view fullName(fullNameBuf, len);

#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
  {
    std::shared_lock lock(Internal::classCacheMutex);
    if (auto it = Internal::classCache.find(image);
        it != Internal::classCache.end()) {
      if (auto it2 = it->second.find(fullName); it2 != it->second.end())
        return it2->second;
    }
  }
#else
  if (auto it = Internal::classCache.find(image);
      it != Internal::classCache.end()) {
    if (auto it2 = it->second.find(fullName); it2 != it->second.end())
      return it2->second;
  }
#endif

  IL2CPP::Il2CppClass *result = nullptr;

#ifdef BNM_CLASSES_MANAGEMENT
  // Check if it's a BNM custom image
  if (image->nameToClassHashTable ==
      (decltype(image->nameToClassHashTable))bnmImageSentinel) {
    ClassesManagement::bnmClassesMap.ForEachByImage(
        image,
        [&_namespace, &_name, &result](IL2CPP::Il2CppClass *BNM_class) -> bool {
          if (_namespace != BNM_class->namespaze || _name != BNM_class->name)
            return false;
          result = BNM_class;
          return true;
        });
  } else
#endif
  {
    if (Internal::il2cppMethods.il2cpp_image_get_class) {
      size_t typeCount = image->typeCount;
      for (size_t i = 0; i < typeCount; ++i) {
        auto cls = il2cppMethods.il2cpp_image_get_class(image, i);
        if (cls->declaringType ||
            (!cls->flags && strcmp(cls->name, BNM_OBFUSCATE("<Module>")) == 0))
          continue;
        if (_namespace == cls->namespaze && _name == cls->name) {
          result = cls;
          break;
        }
      }
    } else {
      std::vector<IL2CPP::Il2CppClass *> classes{};
      Internal::Image$$GetTypes(image, false, &classes);
      for (auto cls : classes) {
        if (!cls)
          continue;
        Internal::il2cppMethods.Class$$Init(cls);
        if (cls->declaringType)
          continue;
        if (cls->namespaze == _namespace && cls->name == _name) {
          result = cls;
          break;
        }
      }
    }
  }

  if (result) {
#ifdef BNM_ALLOW_MULTI_THREADING_SYNC
    std::unique_lock writeLock(Internal::classCacheMutex);
#endif
    Internal::classCache[image][std::string(fullName)] = result;
  }

  return result;
}
Class Internal::TryMakeGenericClass(
    Class genericType, const std::vector<CompileTimeClass> &templateTypes) {
  if (!vmData.RuntimeType$$MakeGenericType.IsValid())
    return {};
  auto monoType = genericType.GetMonoType();
  auto monoGenericsList =
      Structures::Mono::Array<MonoType *>::Create(templateTypes.size(), true);
  for (IL2CPP::il2cpp_array_size_t i = 0;
       i < (IL2CPP::il2cpp_array_size_t)templateTypes.size(); ++i)
    (*monoGenericsList)[i] = templateTypes[i].ToClass().GetMonoType();

  Class typedGenericType =
      vmData.RuntimeType$$MakeGenericType(monoType, monoGenericsList);

  monoGenericsList->Destroy();

  return typedGenericType;
}

MethodBase Internal::TryMakeGenericMethod(
    const MethodBase &genericMethod,
    const std::vector<CompileTimeClass> &templateTypes) {
  if (!vmData.RuntimeMethodInfo$$MakeGenericMethod_impl.IsValid() ||
      !genericMethod.GetInfo()->is_generic)
    return {};
  IL2CPP::Il2CppReflectionMethod reflectionMethod;
  reflectionMethod.method = genericMethod.GetInfo();

  // Il2cpp don't care about it
#ifdef BNM_CHECK_INSTANCE_TYPE
  reflectionMethod.object.klass = BNM::PRIVATE_INTERNAL::GetMethodClass(
      vmData.RuntimeMethodInfo$$MakeGenericMethod_impl._data);
#endif
  auto monoGenericsList =
      Structures::Mono::Array<MonoType *>::Create(templateTypes.size(), true);
  for (IL2CPP::il2cpp_array_size_t i = 0;
       i < (IL2CPP::il2cpp_array_size_t)templateTypes.size(); ++i)
    (*monoGenericsList)[i] = templateTypes[i].ToClass().GetMonoType();

  MethodBase typedGenericMethod =
      vmData
          .RuntimeMethodInfo$$MakeGenericMethod_impl[(void *)&reflectionMethod](
              monoGenericsList)
          ->method;

  monoGenericsList->Destroy();

  return typedGenericMethod;
}

Class Internal::GetPointer(Class target) {
  if (!vmData.RuntimeType$$MakePointerType.IsValid())
    return {};
  return vmData.RuntimeType$$MakePointerType(target.GetMonoType());
}

Class Internal::GetReference(Class target) {
  if (!vmData.RuntimeType$$make_byref_type.IsValid())
    return {};
  return vmData.RuntimeType$$make_byref_type[(void *)target.GetMonoType()]();
}