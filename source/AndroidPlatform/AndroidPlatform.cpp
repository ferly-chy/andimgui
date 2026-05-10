#include "AndroidPlatform.h"

#include "Core/ElfScannerManager.h"
#include "Utils/Logger.h"

#include <jni.h>

struct android_app *g_App = nullptr;

JavaVM* g_JavaVM = nullptr; /// deprecated, use AndroidPlatform::GetJavaVM() instead

namespace {
ANativeWindow* g_CachedWindow = nullptr;

class ScopedJniThreadAttachment {
public:
    explicit ScopedJniThreadAttachment(JavaVM* vm) noexcept : vm_(vm) {
        if (!vm_) {
            return;
        }

        if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_OK) {
            return;
        }

        if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
            attached_ = true;
        } else {
            env_ = nullptr;
        }
    }

    ~ScopedJniThreadAttachment() noexcept {
        if (attached_ && vm_) {
            vm_->DetachCurrentThread();
        }
    }

    ScopedJniThreadAttachment(const ScopedJniThreadAttachment&) = delete;
    ScopedJniThreadAttachment& operator=(const ScopedJniThreadAttachment&) = delete;
    ScopedJniThreadAttachment(ScopedJniThreadAttachment&&) = delete;
    ScopedJniThreadAttachment& operator=(ScopedJniThreadAttachment&&) = delete;

    [[nodiscard]] JNIEnv* env() const noexcept { return env_; }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

class ScopedLocalFrame {
public:
    ScopedLocalFrame(JNIEnv* env, jint capacity) noexcept
        : env_(env), pushed_(env_ && env_->PushLocalFrame(capacity) == JNI_OK) {}

    ~ScopedLocalFrame() noexcept {
        if (pushed_ && env_) {
            env_->PopLocalFrame(nullptr);
        }
    }

    ScopedLocalFrame(const ScopedLocalFrame&) = delete;
    ScopedLocalFrame& operator=(const ScopedLocalFrame&) = delete;
    ScopedLocalFrame(ScopedLocalFrame&&) = delete;
    ScopedLocalFrame& operator=(ScopedLocalFrame&&) = delete;

    [[nodiscard]] bool pushed() const noexcept { return pushed_; }

private:
    JNIEnv* env_ = nullptr;
    bool pushed_ = false;
};

bool ClearPendingJniException(JNIEnv* env) noexcept {
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

template <typename T>
[[nodiscard]] T RequireNoJniException(JNIEnv* env, T value) noexcept {
    const bool hadException = ClearPendingJniException(env);
    if (!value || hadException) {
        return nullptr;
    }
    return value;
}

[[nodiscard]] jclass FindClassChecked(JNIEnv* env, const char* className) noexcept {
    return RequireNoJniException(env, env->FindClass(className));
}

[[nodiscard]] jmethodID GetMethodIDChecked(JNIEnv* env, jclass clazz, const char* name, const char* signature) noexcept {
    return RequireNoJniException(env, env->GetMethodID(clazz, name, signature));
}

[[nodiscard]] jmethodID GetStaticMethodIDChecked(JNIEnv* env, jclass clazz, const char* name, const char* signature) noexcept {
    return RequireNoJniException(env, env->GetStaticMethodID(clazz, name, signature));
}

[[nodiscard]] jfieldID GetFieldIDChecked(JNIEnv* env, jclass clazz, const char* name, const char* signature) noexcept {
    return RequireNoJniException(env, env->GetFieldID(clazz, name, signature));
}

[[nodiscard]] jobject GetObjectFieldChecked(JNIEnv* env, jobject object, jfieldID field) noexcept {
    return RequireNoJniException(env, env->GetObjectField(object, field));
}

[[nodiscard]] jobject CallObjectMethodChecked(JNIEnv* env, jobject object, jmethodID method, auto... args) noexcept {
    return RequireNoJniException(env, env->CallObjectMethod(object, method, args...));
}

} // namespace

namespace AndroidPlatform {

/**
 * 通过 ElfScanner 解析 libart.so 的 JNI_GetCreatedJavaVMs 符号获取 JavaVM 指针
 */
JavaVM* GetJavaVM()
{
    static JavaVM* s_vm = nullptr;
    if (s_vm) return s_vm;

    auto addr = Elf.art().findSymbol("JNI_GetCreatedJavaVMs");
    if (!addr)
    {
        LOGE("[AndroidPlatform] GetJavaVM: findSymbol(JNI_GetCreatedJavaVMs) failed");
        return nullptr;
    }

    using JNI_GetCreatedJavaVMs_t = jint (*)(JavaVM**, jsize, jsize*);
    auto fn = reinterpret_cast<JNI_GetCreatedJavaVMs_t>(addr);

    JavaVM* vm = nullptr;
    jsize count = 0;
    if (fn(&vm, 1, &count) == JNI_OK && count > 0)
    {
        s_vm = vm;
        LOGI("[AndroidPlatform] GetJavaVM: got VM=%p", s_vm);
        return s_vm;
    }

    LOGE("[AndroidPlatform] GetJavaVM: JNI_GetCreatedJavaVMs failed, count=%d", static_cast<int>(count));
    return nullptr;
}

JNIEnv *GetJavaEnv()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] GetJavaEnv: JavaVM is null");
        return nullptr;
    }
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK)
        return env;
    LOGW("[AndroidPlatform] GetJavaEnv: GetEnv failed (thread not attached?)");
    return nullptr;
}

/**
 * 通过 JNI 反射获取 android_app*，适用于所有使用 NativeActivity + android_native_app_glue 的 Android 应用
 *
 * 原理：
 *   ActivityThread.mActivities -> ActivityClientRecord.activity -> NativeActivity
 *   NativeActivity.mNativeHandle (long) 即 ANativeActivity*
 *   ANativeActivity::instance 由 glue code 设置为 android_app*
 */
android_app* FindAndroidAppViaJNI()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] FindAndroidAppViaJNI: JavaVM is null");
        return nullptr;
    }

    ScopedJniThreadAttachment attachment{vm};
    JNIEnv* env = attachment.env();
    if (!env)
    {
        LOGE("[AndroidPlatform] FindAndroidAppViaJNI: AttachCurrentThread failed");
        return nullptr;
    }

    android_app* result = nullptr;

    // PushLocalFrame 确保所有 JNI 局部引用在 PopLocalFrame 时自动释放
    const ScopedLocalFrame localFrame{env, 32};
    if (localFrame.pushed())
    {
        do {
            // 1. ActivityThread.currentActivityThread()
            jclass atClass = FindClassChecked(env, "android/app/ActivityThread");
            if (!atClass) { break; }

            jmethodID catMethod = GetStaticMethodIDChecked(env, atClass, "currentActivityThread",
                "()Landroid/app/ActivityThread;");
            if (!catMethod) { break; }

            jobject at = RequireNoJniException(env, env->CallStaticObjectMethod(atClass, catMethod));
            if (!at) { break; }

            // 2. mActivities: ArrayMap<IBinder, ActivityClientRecord> (API 21+)
            jfieldID activitiesField = GetFieldIDChecked(env, atClass, "mActivities",
                "Landroid/util/ArrayMap;");
            if (!activitiesField) { break; }

            jobject activities = GetObjectFieldChecked(env, at, activitiesField);
            if (!activities) { break; }

            // 3. 遍历 ArrayMap 获取 ActivityClientRecord
            jclass mapClass = RequireNoJniException(env, env->GetObjectClass(activities));
            if (!mapClass) { break; }
            jmethodID sizeMethod = GetMethodIDChecked(env, mapClass, "size", "()I");
            jmethodID valueAtMethod = GetMethodIDChecked(env, mapClass, "valueAt",
                "(I)Ljava/lang/Object;");
            if (!sizeMethod || !valueAtMethod) break;

            jint size = env->CallIntMethod(activities, sizeMethod);

            // 4. 查找 NativeActivity（含子类）并读取 mNativeHandle
            jclass naClass = FindClassChecked(env, "android/app/NativeActivity");
            if (!naClass) { break; }

            jfieldID handleField = GetFieldIDChecked(env, naClass, "mNativeHandle", "J");
            if (!handleField) { break; }

            for (jint i = 0; i < size; i++)
            {
                jobject record = CallObjectMethodChecked(env, activities, valueAtMethod, i);
                if (!record) continue;

                jclass recClass = RequireNoJniException(env, env->GetObjectClass(record));
                if (!recClass) { continue; }
                jfieldID actField = GetFieldIDChecked(env, recClass, "activity",
                    "Landroid/app/Activity;");
                if (!actField) { continue; }

                jobject activity = GetObjectFieldChecked(env, record, actField);
                if (!activity) continue;

                // IsInstanceOf 会匹配 NativeActivity 及其所有子类（如 UE4 GameActivity）
                if (env->IsInstanceOf(activity, naClass))
                {
                    jlong handle = env->GetLongField(activity, handleField);
                    LOGI("[AndroidPlatform] FindAndroidAppViaJNI: NativeActivity found, mNativeHandle=0x%llx", static_cast<unsigned long long>(handle));
                    if (handle != 0)
                    {
                        auto* na = reinterpret_cast<ANativeActivity*>(handle);
                        result = static_cast<android_app*>(na->instance);
                        LOGI("[AndroidPlatform] FindAndroidAppViaJNI: android_app=%p", result);
                    }
                }
                if (result) break;
            }
        } while (false);
    }

    ClearPendingJniException(env);
    return result;
}

/**
 * 通过 JNI 反射从当前 Activity 的 Window → DecorView → ViewRootImpl → Surface 获取 ANativeWindow*
 *
 * 原理：
 *   ActivityThread.currentActivityThread().mActivities → ActivityClientRecord.activity
 *   Activity.getWindow().getDecorView().getViewRootImpl().mSurface
 *   → ANativeWindow_fromSurface(env, surface)
 *
 * 适用于所有 Activity（包括 Godot/Unity 等不使用 NativeActivity 的引擎）
 */
ANativeWindow* FindNativeWindowViaJNI()
{
    JavaVM* vm = GetJavaVM();
    if (!vm)
    {
        LOGE("[AndroidPlatform] FindNativeWindowViaJNI: JavaVM is null");
        return nullptr;
    }

    ScopedJniThreadAttachment attachment{vm};
    JNIEnv* env = attachment.env();
    if (!env)
    {
        LOGE("[AndroidPlatform] FindNativeWindowViaJNI: AttachCurrentThread failed");
        return nullptr;
    }

    ANativeWindow* result = nullptr;

    const ScopedLocalFrame localFrame{env, 32};
    if (localFrame.pushed())
    {
        do {
            // 1. ActivityThread.currentActivityThread()
            jclass atClass = FindClassChecked(env, "android/app/ActivityThread");
            if (!atClass) { break; }

            jmethodID catMethod = GetStaticMethodIDChecked(env, atClass, "currentActivityThread",
                "()Landroid/app/ActivityThread;");
            if (!catMethod) { break; }

            jobject at = RequireNoJniException(env, env->CallStaticObjectMethod(atClass, catMethod));
            if (!at) { break; }

            // 2. mActivities: ArrayMap<IBinder, ActivityClientRecord>
            jfieldID activitiesField = GetFieldIDChecked(env, atClass, "mActivities",
                "Landroid/util/ArrayMap;");
            if (!activitiesField) { break; }

            jobject activities = GetObjectFieldChecked(env, at, activitiesField);
            if (!activities) { break; }

            // 3. 遍历 ArrayMap
            jclass mapClass = RequireNoJniException(env, env->GetObjectClass(activities));
            if (!mapClass) { break; }
            jmethodID sizeMethod = GetMethodIDChecked(env, mapClass, "size", "()I");
            jmethodID valueAtMethod = GetMethodIDChecked(env, mapClass, "valueAt",
                "(I)Ljava/lang/Object;");
            if (!sizeMethod || !valueAtMethod) break;

            jint size = env->CallIntMethod(activities, sizeMethod);

            for (jint i = 0; i < size; i++)
            {
                jobject record = CallObjectMethodChecked(env, activities, valueAtMethod, i);
                if (!record) continue;

                // record.activity
                jclass recClass = RequireNoJniException(env, env->GetObjectClass(record));
                if (!recClass) { continue; }
                jfieldID actField = GetFieldIDChecked(env, recClass, "activity",
                    "Landroid/app/Activity;");
                if (!actField) { continue; }

                jobject activity = GetObjectFieldChecked(env, record, actField);
                if (!activity) continue;

                // Activity.getWindow()
                jclass actClass = FindClassChecked(env, "android/app/Activity");
                if (!actClass) { continue; }

                jmethodID getWindowMethod = GetMethodIDChecked(env, actClass, "getWindow",
                    "()Landroid/view/Window;");
                if (!getWindowMethod) { continue; }

                jobject window = CallObjectMethodChecked(env, activity, getWindowMethod);
                if (!window) { continue; }

                // Window.getDecorView()
                jclass windowClass = RequireNoJniException(env, env->GetObjectClass(window));
                if (!windowClass) { continue; }
                jmethodID getDecorViewMethod = GetMethodIDChecked(env, windowClass, "getDecorView",
                    "()Landroid/view/View;");
                if (!getDecorViewMethod) { continue; }

                jobject decorView = CallObjectMethodChecked(env, window, getDecorViewMethod);
                if (!decorView) { continue; }

                // View.getViewRootImpl()（隐藏 API，所有 Android 版本均存在）
                jclass viewClass = FindClassChecked(env, "android/view/View");
                if (!viewClass) { continue; }

                jmethodID getVRIMethod = GetMethodIDChecked(env, viewClass, "getViewRootImpl",
                    "()Landroid/view/ViewRootImpl;");
                if (!getVRIMethod) { continue; }

                jobject vri = CallObjectMethodChecked(env, decorView, getVRIMethod);
                if (!vri) { continue; }

                // ViewRootImpl.mSurface
                jclass vriClass = RequireNoJniException(env, env->GetObjectClass(vri));
                if (!vriClass) { continue; }
                jfieldID surfaceField = GetFieldIDChecked(env, vriClass, "mSurface",
                    "Landroid/view/Surface;");
                if (!surfaceField) { continue; }

                jobject surface = GetObjectFieldChecked(env, vri, surfaceField);
                if (!surface) { continue; }

                // Surface.isValid() 检查
                jclass surfClass = RequireNoJniException(env, env->GetObjectClass(surface));
                if (!surfClass) { continue; }
                jmethodID isValidMethod = GetMethodIDChecked(env, surfClass, "isValid", "()Z");
                if (isValidMethod)
                {
                    jboolean valid = env->CallBooleanMethod(surface, isValidMethod);
                    if (!valid)
                    {
                        LOGW("[AndroidPlatform] FindNativeWindowViaJNI: Surface not valid for activity[%d]", static_cast<int>(i));
                        continue;
                    }
                }
                if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }

                // ANativeWindow_fromSurface — 返回的 window 已被 acquire
                result = ANativeWindow_fromSurface(env, surface);
                if (result)
                {
                    LOGI("[AndroidPlatform] FindNativeWindowViaJNI: ANativeWindow=%p (%dx%d)",
                         result, ANativeWindow_getWidth(result), ANativeWindow_getHeight(result));
                }
                if (result) break;
            }
        } while (false);
    }

    ClearPendingJniException(env);
    return result;
}

ANativeWindow* GetNativeWindow()
{
    if (g_App && g_App->window)
        return g_App->window;

    auto isUsableWindow = [](ANativeWindow* window) -> bool {
        return window && ANativeWindow_getWidth(window) > 0 && ANativeWindow_getHeight(window) > 0;
    };

    if (isUsableWindow(g_CachedWindow))
        return g_CachedWindow;

    if (g_CachedWindow)
    {
        LOGW("[AndroidPlatform] GetNativeWindow: releasing stale cached window=%p", g_CachedWindow);
        ANativeWindow_release(g_CachedWindow);
        g_CachedWindow = nullptr;
    }

    g_CachedWindow = FindNativeWindowViaJNI();
    return g_CachedWindow;
}

void ResetNativeWindowCache()
{
    if (g_CachedWindow)
    {
        ANativeWindow_release(g_CachedWindow);
        g_CachedWindow = nullptr;
    }
}

}
