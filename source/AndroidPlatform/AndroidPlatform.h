#pragma once

// Android 平台基础：全局 android_app / JavaVM / JNI 环境获取
// 整个项目通过此头文件访问平台上下文

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include "android_native_app_glue.h"
#include <jni.h>

extern struct android_app *g_App;

extern JavaVM* g_JavaVM; /// deprecated, use AndroidPlatform::GetJavaVM() instead

namespace AndroidPlatform {

[[nodiscard]] JavaVM* GetJavaVM();

[[nodiscard]] JNIEnv* GetJavaEnv();

[[nodiscard]] android_app* FindAndroidAppViaJNI();

[[nodiscard]] ANativeWindow* FindNativeWindowViaJNI();

[[nodiscard]] ANativeWindow* GetNativeWindow();

void ResetNativeWindowCache();

}
