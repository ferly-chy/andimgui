#include <BNM/Class.hpp>
#include <BNM/DebugMessages.hpp>
#include <BNM/EventBase.hpp>
#include <BNM/FieldBase.hpp>
#include <BNM/MethodBase.hpp>
#include <BNM/PropertyBase.hpp>
#include <Internals.hpp>

using namespace BNM;

Class::Class(const IL2CPP::Il2CppObject *object) {
  if (!object)
    return;
  _data = object->klass;
}

Class::Class(const IL2CPP::Il2CppType *type) {
  if (!type)
    return;
  _data = Internal::il2cppMethods.il2cpp_class_from_il2cpp_type(type);
}

Class::Class(const MonoType *type) {
  if (!type)
    return;
  _data = Internal::il2cppMethods.il2cpp_class_from_il2cpp_type(type->type);
}

Class::Class(const CompileTimeClass &compileTimeClass) {
  _data = compileTimeClass;
}

static IL2CPP::Il2CppClass *
TryGetClassWithoutImage(const std::string_view &_namespace,
                        const std::string_view &_name) {
  std::string fullName;
  fullName.reserve(_namespace.size() + _name.size() + 3);
  if (!_namespace.empty()) {
    fullName += _namespace;
    fullName += "::";
  }
  fullName += _name;

  {
    std::shared_lock lock(Internal::globalClassCacheMutex);
    if (auto it = Internal::globalClassCache.find(fullName);
        it != Internal::globalClassCache.end() && !it->second.empty())
      return it->second.front();
  }

  auto &assemblies = *Internal::il2cppMethods.Assembly$$GetAllAssemblies();
  std::vector<IL2CPP::Il2CppClass *> matches{};

  for (auto assembly : assemblies) {
    auto image = Internal::il2cppMethods.il2cpp_assembly_get_image(assembly);
    if (auto _data = Internal::TryGetClassInImage(image, _namespace, _name);
        _data)
      matches.push_back(_data);
  }

  if (!matches.empty()) {
    std::unique_lock writeLock(Internal::globalClassCacheMutex);
    // Keep all matches so image-less lookup does not permanently hide assembly
    // collisions.
    Internal::globalClassCache[fullName] = matches;
    return matches.front();
  }

  return nullptr;
}

Class::Class(const std::string_view &_namespace,
             const std::string_view &_name) {
  if (_data = TryGetClassWithoutImage(_namespace, _name); _data)
    return;
  auto namespaceStr = std::string(_namespace);
  auto nameStr = std::string(_name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_Constructor_NotFound, namespaceStr.c_str(),
               nameStr.c_str());
}

Class::Class(const std::string_view &_namespace, const std::string_view &_name,
             const BNM::Image &image) {
  if (!image) {
    auto imageStr = std::string(image.str());
    auto namespaceStr = std::string(_namespace);
    auto nameStr = std::string(_name);
    BNM_LOG_WARN(DBG_BNM_MSG_Class_Constructor_Image_Warn, imageStr.c_str(),
                 namespaceStr.c_str(), nameStr.c_str());
    _data = nullptr;
    return;
  }

  if (_data = Internal::TryGetClassInImage(image, _namespace, _name); _data)
    return;

  auto imageStr = std::string(image.str());
  auto namespaceStr = std::string(_namespace);
  auto nameStr = std::string(_name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_Constructor_Image_NotFound, imageStr.c_str(),
               namespaceStr.c_str(), nameStr.c_str());
}

std::span<const BNM::IL2CPP::MethodInfo *> Class::GetMethodsSpan() const {
  if (!_data)
    return {};
  TryInit();
  return {_data->methods, _data->method_count};
}

std::span<BNM::IL2CPP::FieldInfo> Class::GetFieldsSpan() const {
  if (!_data)
    return {};
  TryInit();
  return {_data->fields, _data->field_count};
}

std::span<BNM::IL2CPP::Il2CppClass *> Class::GetInnerClassesSpan() const {
  if (!_data)
    return {};
  TryInit();
  return {_data->nestedTypes, _data->nested_type_count};
}

std::span<BNM::IL2CPP::Il2CppClass *> Class::GetInterfacesSpan() const {
  if (!_data)
    return {};
  TryInit();
  return {_data->implementedInterfaces, _data->interfaces_count};
}

std::vector<Class> Class::GetInnerClasses(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<Class> ret{};
  Class curClass = *this;

  do {
    for (auto cls : curClass.GetInnerClassesSpan())
      ret.emplace_back(cls);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

std::vector<FieldBase> Class::GetFields(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<FieldBase> ret{};
  Class curClass = *this;

  do {
    for (auto &field : curClass.GetFieldsSpan())
      ret.emplace_back(&field);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

std::vector<MethodBase> Class::GetMethods(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<MethodBase> ret{};
  Class curClass = *this;

  do {
    for (auto method : curClass.GetMethodsSpan())
      ret.emplace_back(method);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

std::vector<BNM::Class> Class::GetInterfaces(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<Class> ret{};
  Class curClass = *this;

  do {
    for (auto cls : curClass.GetInterfacesSpan())
      ret.emplace_back(cls);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

std::vector<PropertyBase> Class::GetProperties(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<PropertyBase> ret{};
  Class curClass = *this;

  do {
    auto data = curClass._data;
    auto end = data->properties + data->property_count;
    for (auto currentProperty = data->properties; currentProperty != end;
         ++currentProperty)
      ret.emplace_back(currentProperty);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

std::vector<EventBase> Class::GetEvents(bool includeParent) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  std::vector<EventBase> ret{};
  Class curClass = *this;

  do {
    auto data = curClass._data;
    auto end = data->events + data->event_count;
    for (auto currentEvent = data->events; currentEvent != end; ++currentEvent)
      ret.emplace_back(currentEvent);
    if (includeParent)
      curClass = curClass.GetParent();
    else
      break;
  } while (curClass._data);

  return ret;
}

MethodBase Class::GetMethod(const std::string_view &name,
                            int parameters) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();

  std::string fullName;
  fullName.reserve(name.size() + 12);
  fullName += name;
  fullName += '#';
  fullName += std::to_string(parameters);

  std::shared_lock lock(Internal::methodCacheMutex);
  if (auto it = Internal::methodCache.find(_data);
      it != Internal::methodCache.end()) {
    if (auto it2 = it->second.find(fullName); it2 != it->second.end())
      return it2->second;
  }
  lock.unlock();

  auto method = Internal::IterateMethods(
      *this, [&name, &parameters](IL2CPP::MethodInfo *method) {
        return name == method->name &&
               (method->parameters_count == parameters || parameters == -1);
      });

  if (method != nullptr) {
    std::unique_lock writeLock(Internal::methodCacheMutex);
    Internal::methodCache[_data][fullName] = method;
    return method;
  }

  auto methodName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetMethod_Count_NotFound, _data->namespaze,
               _data->name, methodName.c_str(), parameters);
  return {};
}

MethodBase Class::GetMethod(
    const std::string_view &name,
    const std::initializer_list<std::string_view> &parameterNames) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();

  auto parameters = (uint8_t)parameterNames.size();

  std::string fullName;
  fullName.reserve(name.size() + parameters * 10 + 5);
  fullName += name;
  fullName += "(n:";
  for (auto &paramName : parameterNames) {
    fullName += paramName;
    fullName += ",";
  }
  fullName += ")";

  std::shared_lock lock(Internal::methodCacheMutex);
  if (auto it = Internal::methodCache.find(_data);
      it != Internal::methodCache.end()) {
    if (auto it2 = it->second.find(fullName); it2 != it->second.end())
      return it2->second;
  }
  lock.unlock();

  auto method = Internal::IterateMethods(
      *this, [&name, &parameters, &parameterNames](IL2CPP::MethodInfo *method) {
        if (name != method->name || method->parameters_count != parameters)
          return false;
        for (uint8_t i = 0; i < parameters; ++i)
          if (Internal::il2cppMethods.il2cpp_method_get_param_name(method, i) !=
              parameterNames.begin()[i])
            return false;
        return true;
      });

  if (method != nullptr) {
    std::unique_lock writeLock(Internal::methodCacheMutex);
    Internal::methodCache[_data][fullName] = method;
    return method;
  }

  auto methodName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetMethod_Names_NotFound, _data->namespaze,
               _data->name, methodName.c_str(), parameters);
  return {};
}

MethodBase Class::GetMethod(
    const std::string_view &name,
    const std::initializer_list<CompileTimeClass> &parameterTypes) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto parameters = (uint8_t)parameterTypes.size();

  std::string fullName;
  fullName.reserve(name.size() + parameters * 16 + 5);
  fullName += name;
  fullName += "(t:";
  for (auto &type : parameterTypes) {
    fullName += std::to_string((BNM_PTR)type.ToIl2CppClass());
    fullName += ",";
  }
  fullName += ")";

  std::shared_lock lock(Internal::methodCacheMutex);
  if (auto it = Internal::methodCache.find(_data);
      it != Internal::methodCache.end()) {
    if (auto it2 = it->second.find(fullName); it2 != it->second.end())
      return it2->second;
  }
  lock.unlock();

  auto method = Internal::IterateMethods(
      *this, [&name, parameters, &parameterTypes](IL2CPP::MethodInfo *method) {
        if (name != method->name || method->parameters_count != parameters)
          return false;
        for (uint8_t i = 0; i < parameters; ++i) {
#if UNITY_VER < 212
          auto param = (method->parameters + i)->parameter_type;
#else
            auto param = method->parameters[i];
#endif

          if (Class(param).GetClass() !=
              parameterTypes.begin()[i].ToIl2CppClass())
            return false;
        }
        return true;
      });

  if (method != nullptr) {
    std::unique_lock writeLock(Internal::methodCacheMutex);
    Internal::methodCache[_data][fullName] = method;
    return method;
  }

  auto methodName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetMethod_Types_NotFound, _data->namespaze,
               _data->name, methodName.c_str(), parameters);
  return {};
}

PropertyBase Class::GetProperty(const std::string_view &name) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto curClass = _data;

  do {
    auto end = curClass->properties + curClass->property_count;
    for (auto currentProperty = curClass->properties; currentProperty != end;
         ++currentProperty)
      if (name == currentProperty->name)
        return currentProperty;
    curClass = curClass->parent;
  } while (curClass);

  auto propertyName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetProperty_NotFound, str().c_str(),
               propertyName.c_str());
  return {};
}

PropertyBase Class::GetProperty(const std::string_view &name,
                                const CompileTimeClass &type) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto curClass = _data;

  auto typeClass = type.ToIl2CppClass();

  do {
    auto end = curClass->properties + curClass->property_count;
    for (auto currentProperty = curClass->properties; currentProperty != end;
         ++currentProperty)
      if (name == currentProperty->name &&
          PropertyBase{currentProperty}.GetType() == typeClass)
        return currentProperty;
    curClass = curClass->parent;
  } while (curClass);

  auto propertyName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetProperty_NotFound, str().c_str(),
               propertyName.c_str());
  return {};
}

Class Class::GetInnerClass(const std::string_view &name) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto curClass = _data;

  do {
    for (uint16_t i = 0; i < curClass->nested_type_count; ++i) {
      auto cls = curClass->nestedTypes[i];
      if (name == cls->name)
        return cls;
    }
    curClass = curClass->parent;
  } while (curClass);

  auto innerClassName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetInnerClass_NotFound, _data->namespaze,
               _data->name, innerClassName.c_str());
  return {};
}

FieldBase Class::GetField(const std::string_view &name) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto curClass = _data;

  do {
    auto end = curClass->fields + curClass->field_count;
    for (auto currentField = curClass->fields; currentField != end;
         ++currentField) {
      if (name != currentField->name)
        continue;
      return currentField;
    }
    curClass = curClass->parent;
  } while (curClass);

  auto fieldName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetField_NotFound, _data->namespaze,
               _data->name, fieldName.c_str());
  return {};
}

EventBase Class::GetEvent(const std::string_view &name) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  auto curClass = _data;

  do {
    auto end = curClass->events + curClass->event_count;
    for (auto currentEvent = curClass->events; currentEvent != end;
         ++currentEvent) {
      if (name != currentEvent->name)
        continue;
      return currentEvent;
    }
    curClass = curClass->parent;
  } while (curClass);

  auto eventName = std::string(name);
  BNM_LOG_WARN(DBG_BNM_MSG_Class_GetEvent_NotFound, _data->namespaze,
               _data->name, eventName.c_str());
  return {};
}

Class Class::GetParent() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return _data->parent;
}

Class Class::GetArray() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return Internal::il2cppMethods.il2cpp_array_class_get(_data, 1);
}

Class Class::GetPointer() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return Internal::GetPointer(*this);
}

Class Class::GetReference() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return Internal::GetReference(*this);
}

Class Class::GetGeneric(
    const std::initializer_list<CompileTimeClass> &templateTypes) const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return Internal::TryMakeGenericClass(*this, templateTypes);
}

IL2CPP::Il2CppType *Class::GetIl2CppType() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return nullptr;
  TryInit();
#if UNITY_VER > 174
  return (IL2CPP::Il2CppType *)&_data->byval_arg;
#else
  return (IL2CPP::Il2CppType *)_data->byval_arg;
#endif
}

MonoType *Class::GetMonoType() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return (MonoType *)Internal::il2cppMethods.il2cpp_type_get_object(
      GetIl2CppType());
}

CompileTimeClass Class::GetCompileTimeClass() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  TryInit();
  CompileTimeClass result{};
  result._loadedClass = *this;
  return result;
}

Image Class::GetImage() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();
  return {_data->image};
}

Class::operator BNM::CompileTimeClass() const { return GetCompileTimeClass(); }

IL2CPP::Il2CppObject *Class::CreateNewInstance() const {
  BNM_LOG_ERR_IF(!_data, DBG_BNM_MSG_Class_Dead_Error);
  if (!_data)
    return {};
  TryInit();

  if ((_data->flags &
       (0x00000080 |
        0x00000020))) // TYPE_ATTRIBUTE_ABSTRACT | TYPE_ATTRIBUTE_INTERFACE
    BNM_LOG_WARN(DBG_BNM_MSG_Class_CreateNewInstance_Abstract_Warn,
                 str().c_str());

  return Internal::il2cppMethods.il2cpp_object_new(_data);
}

// Try initializing the class if it is alive
void Class::TryInit() const {
  if (_data)
    Internal::il2cppMethods.Class$$Init(_data);
}

IL2CPP::Il2CppObject *Class::BoxObject(IL2CPP::Il2CppClass *_data, void *data) {
  return Internal::il2cppMethods.il2cpp_value_box(_data, data);
}

IL2CPP::Il2CppArray *Class::ArrayNew(IL2CPP::Il2CppClass *cls,
                                     IL2CPP::il2cpp_array_size_t length) {
  return Internal::il2cppMethods.il2cpp_array_new(cls, length);
}

void *Class::NewListInstance() {
  return Internal::customListTemplateClass.CreateNewInstance();
}

Class Class::GetListClass() { return Internal::vmData.System$$List; }

namespace CompileTimeClassProcessors {
typedef void (*ProcessorType)(CompileTimeClass &target,
                              CompileTimeClass::_BaseInfo *info);

static void Warn(CompileTimeClass &, CompileTimeClass::_BaseInfo *) {
  BNM_LOG_WARN(DBG_BNM_MSG_CompileTimeClass_ToClass_default_Warn);
}

// _ClassInfo
static void ProcessClassInfo(CompileTimeClass &target,
                             CompileTimeClass::_BaseInfo *info) {
  auto classInfo = (CompileTimeClass::_ClassInfo *)info;

  auto _namespace =
      classInfo->_namespace ? classInfo->_namespace : std::string_view{};

  if (!classInfo->_imageName || !strlen(classInfo->_imageName)) {
    target._loadedClass = TryGetClassWithoutImage(_namespace, classInfo->_name);
    return;
  }

  BNM::Image image{};

  auto &assemblies = *Internal::il2cppMethods.Assembly$$GetAllAssemblies();
  for (auto assembly : assemblies) {
    auto currentImage =
        Internal::il2cppMethods.il2cpp_assembly_get_image(assembly);
    if (!Internal::CompareImageName(currentImage, classInfo->_imageName))
      continue;
    image = currentImage;
    break;
  }

  target._loadedClass =
      Internal::TryGetClassInImage(image, _namespace, classInfo->_name);
}

// _InnerInfo
static void ProcessInnerInfo(CompileTimeClass &target,
                             CompileTimeClass::_BaseInfo *info) {
  if (!target._loadedClass) {
    BNM_LOG_WARN(DBG_BNM_MSG_CompileTimeClass_ToClass_Inner_Warn);
    return;
  }

  auto innerInfo = (CompileTimeClass::_InnerInfo *)info;

  if (!target._loadedClass)
    return;
  target._loadedClass = target._loadedClass.GetInnerClass(innerInfo->_name);
}

// _ModifierInfo
static void ProcessModifierInfo(CompileTimeClass &target,
                                CompileTimeClass::_BaseInfo *info) {
  if (!target._loadedClass) {
    BNM_LOG_WARN(DBG_BNM_MSG_CompileTimeClass_ToClass_Modifier_Warn);
    return;
  }

  auto modifierInfo = (CompileTimeClass::_ModifierInfo *)info;

  switch (modifierInfo->_modifierType) {
  case CompileTimeClass::ModifierType::Pointer: {
    target._loadedClass = target._loadedClass.GetPointer();
  } break;
  case CompileTimeClass::ModifierType::Reference: {
    target._loadedClass = target._loadedClass.GetReference();
  } break;
  case CompileTimeClass::ModifierType::Array: {
    target._loadedClass = target._loadedClass.GetArray();
  } break;
  case CompileTimeClass::ModifierType::None:
    break;
  }
}

// _GenericInfo
static void ProcessGenericInfo(CompileTimeClass &target,
                               CompileTimeClass::_BaseInfo *info) {
  if (!target._loadedClass) {
    BNM_LOG_WARN(DBG_BNM_MSG_CompileTimeClass_ToClass_Generic_Warn);
    return;
  }

  auto genericInfo = (CompileTimeClass::_GenericInfo *)info;

  // Save memory, even if the user does not want to
  for (auto &type : genericInfo->_types)
    if (!type._stack.IsEmpty())
      ((BNM::CompileTimeClass &)type)._SetBit();
  target._loadedClass =
      Internal::TryMakeGenericClass(target._loadedClass, genericInfo->_types);
  genericInfo->_types.clear();
  genericInfo->_types.shrink_to_fit();
}

ProcessorType processors[(uint8_t)CompileTimeClass::_BaseType::MaxCount] = {
    Warn,
    ProcessClassInfo,
    ProcessInnerInfo,
    ProcessModifierInfo,
    ProcessGenericInfo,
};
} // namespace CompileTimeClassProcessors

Class CompileTimeClass::ToClass() {
  // No reference or autoFree
  if (!_IsBit()) {
    if (_loadedClass)
      return _loadedClass;
    if (_stack.IsEmpty())
      return {(IL2CPP::Il2CppClass *)nullptr};
  }

  // If stack is empty, but bit is set - we have reference
  if (_stack.IsEmpty()) {
    _RemoveBit();
    auto ref = _reference ? *_reference : nullptr;
    _loadedClass = ref;
    _loadedClass.TryInit();
    return _loadedClass;
  }

  bool autoFree = _IsBit();
  _RemoveBit();

  auto lastElement = _stack.lastElement;
  auto current = lastElement->next;
  do {
    auto info = current->value;

    auto index = (uint8_t)info->_baseType;
    if (index >= (uint8_t)_BaseType::MaxCount) {
      BNM_LOG_WARN(DBG_BNM_MSG_CompileTimeClass_ToClass_OoB_Warn,
                   (size_t)index);
      continue;
    }
    CompileTimeClassProcessors::processors[index](*this, (_BaseInfo *)info);

    current = current->next;
  } while (current != lastElement->next);

  if (autoFree)
    Free();

  _loadedClass.TryInit();
  return _loadedClass;
}
Class CompileTimeClass::ToClass() const {
  return ((CompileTimeClass *)this)->ToClass();
}
CompileTimeClass::operator Class() const { return ToClass(); }

IL2CPP::Il2CppType *CompileTimeClass::ToIl2CppType() const {
  return ToClass().GetIl2CppType();
}
CompileTimeClass::operator IL2CPP::Il2CppType *() const {
  return ToIl2CppType();
}

IL2CPP::Il2CppClass *CompileTimeClass::ToIl2CppClass() const {
  return ToClass().GetClass();
}
CompileTimeClass::operator IL2CPP::Il2CppClass *() const {
  return ToIl2CppClass();
}
void CompileTimeClass::Free() {
  if (_stack.IsEmpty())
    return;

  _stack.Clear([](_BaseInfo *info) { BNM_free((void *)info); });
}