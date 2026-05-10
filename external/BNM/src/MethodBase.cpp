#include <BNM/DebugMessages.hpp>
#include <BNM/MethodBase.hpp>
#include <Internals.hpp>

using namespace BNM;

MethodBase::MethodBase(const IL2CPP::MethodInfo *info) {
  if (!BNM::IsAllocated(info))
    return;

  _isStatic = (info->flags & 0x0010) == 0x0010;
  _isVirtual = info->slot != 65535;
  _data = (decltype(_data))info;
}

MethodBase::MethodBase(const IL2CPP::Il2CppReflectionMethod *reflectionMethod) {
  if (!BNM::IsAllocated(reflectionMethod) ||
      !BNM::IsAllocated(reflectionMethod->method))
    return;

  auto info = reflectionMethod->method;

  _isStatic = (info->flags & 0x0010) == 0x0010;
  _isVirtual = info->slot != 65535;
  _data = (decltype(_data))info;
}

MethodBase &MethodBase::SetInstance(IL2CPP::Il2CppObject *val) {
  if (!_data)
    return *this;
  if (_isStatic) {
    BNM_LOG_WARN(DBG_BNM_MSG_MethodBase_SetInstance_Warn, str().c_str());
    return *this;
  }
#ifdef BNM_CHECK_INSTANCE_TYPE
  if (BNM::IsA(val, BNM::PRIVATE_INTERNAL::GetMethodClass(_data)))
    _instance = val;
  else
    BNM_LOG_ERR(DBG_BNM_MSG_MethodBase_SetInstance_Wrong_Instance_Error,
                BNM::Class(val).str().c_str(), str().c_str());
#else
  _instance = val;
#endif
  return *this;
}

MethodBase MethodBase::GetGeneric(
    const std::initializer_list<CompileTimeClass> &templateTypes) const {
  if (!_data)
    return {};
  BNM_LOG_WARN_IF(!_data->is_generic, DBG_BNM_MSG_MethodBase_GetGeneric_Warn,
                  str().c_str());
  if (!_data->is_generic)
    return {};
  return Internal::TryMakeGenericMethod(*this, templateTypes);
}

MethodBase MethodBase::GetOverride() const {
  if (!_data || _isStatic || (_data->flags & 0x0040) == 0)
    return {};
  if (!BNM::IsAllocated(_instance)) {
    BNM_LOG_WARN(DBG_BNM_MSG_MethodBase_Virtualize_Warn, str().c_str());
    return {};
  }

  auto klass = _instance->klass;

  // Fast path: use the method slot if it's valid
  if (_data->slot != 65535 && _data->slot < klass->vtable_count) {
    auto &vTable = klass->vtable[_data->slot];
    if (vTable.method)
      return MethodBase(vTable.method)[_instance];
  }

  // Fallback path: iterate vtable (only if slot indexing failed or was invalid)
  for (uint16_t i = 0; i < klass->vtable_count; ++i) {
    auto &vTable = klass->vtable[i];
    if (!vTable.method)
      continue;

    auto count = vTable.method->parameters_count;
    if (count != _data->parameters_count ||
        strcmp(vTable.method->name, _data->name) != 0)
      continue;

    bool match = true;
    for (uint8_t p = 0; p < count; ++p) {
#if UNITY_VER < 212
      auto type = (vTable.method->parameters + p)->parameter_type;
      auto type2 = (_data->parameters + p)->parameter_type;
#else
      auto type = vTable.method->parameters[p];
      auto type2 = _data->parameters[p];
#endif
      if (Class(type).GetClass() != Class(type2).GetClass()) {
        match = false;
        break;
      }
    }
    if (match)
      return MethodBase(vTable.method)[_instance];
  }
  return {};
}

BNM::Class MethodBase::GetReturnType() const {
  if (!_data)
    return {};
  return _data->return_type;
}

BNM::Class MethodBase::GetParentClass() const {
  if (!_data)
    return {};
  return BNM::PRIVATE_INTERNAL::GetMethodClass(_data);
}
