#pragma once

#include "Class.hpp"
#include "Field.hpp"
#include "Method.hpp"
#include "Property.hpp"

/**
 * @brief Simplified macros for BNM usage to make it "easy to use".
 */

// Easy Class Lookup
// Example: auto myClass = BNM_CLASS("UnityEngine", "GameObject");
#define BNM_CLASS(namespaze, name)                                             \
  BNM::Class(BNM_OBFUSCATE(namespaze), BNM_OBFUSCATE(name))

// Easy Method Lookup and Cast
// Example: auto setActive = BNM_METHOD(myClass, "SetActive", 1).cast<void,
// bool>();
#define BNM_METHOD(klass, name, args) klass.GetMethod(BNM_OBFUSCATE(name), args)

// Easy Field Lookup and Cast
// Example: auto myField = BNM_FIELD(myClass, "myField").cast<int>();
#define BNM_FIELD(klass, name) klass.GetField(BNM_OBFUSCATE(name))

// Easy Property Lookup and Cast
// Example: auto myProp = BNM_PROPERTY(myClass, "myProp").cast<float>();
#define BNM_PROPERTY(klass, name) klass.GetProperty(BNM_OBFUSCATE(name))

// Simplified Hooking Macro
// Usage:
// void (*old_Update)(void*);
// void my_Update(void* instance) {
//    old_Update(instance);
// }
// ...
// BNM_HOOK(BNM_CLASS("UnityEngine", "MonoBehaviour"), "Update", my_Update,
// old_Update);
#define BNM_HOOK(klass, methodName, newFunc, oldFunc)                          \
  BNM::BasicHook(klass.GetMethod(BNM_OBFUSCATE(methodName)), (void *)newFunc,  \
                 (void **)&oldFunc)

// Simplified InvokeHook (Metadata Hook)
#define BNM_INVOKE_HOOK(klass, methodName, newFunc, oldFunc)                   \
  BNM::InvokeHook(klass.GetMethod(BNM_OBFUSCATE(methodName)), (void *)newFunc, \
                  (void **)&oldFunc)

// Automatic thread attachment helper
struct BNMThreadAttacher {
  BNMThreadAttacher() { BNM::AttachIl2Cpp(); }
  ~BNMThreadAttacher() { BNM::DetachIl2Cpp(); }
};

#define BNM_ATTACH_THREAD() BNMThreadAttacher _bnm_attacher;
