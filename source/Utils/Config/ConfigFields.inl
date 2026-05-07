// X-macro 字段表。增删字段改本文件即可, Schema 与序列化注册表自动同步。
//
//   CONFIG_GROUP(label)                    分组标签
//   CONFIG_FIELD(type, name, default)      标量持久化字段
//   CONFIG_ARR(type, name, N, ...)         数组持久化字段 ({...} 含逗号, 走 __VA_ARGS__)
//   CONFIG_NOSAVE / CONFIG_NOSAVE_ARR      不进存档 (运行时开关)
//
// 本文件无 include guard, 调用方负责 #define / #undef。

// 自防御
#ifndef CONFIG_FIELD
#define CONFIG_GROUP(label)
#define CONFIG_FIELD(type, name, ...)
#define CONFIG_ARR(type, name, n, ...)
#define CONFIG_NOSAVE(type, name, ...)
#define CONFIG_NOSAVE_ARR(type, name, n, ...)
#define _CONFIG_FIELDS_INL_SELFGUARD
#endif

CONFIG_GROUP("配置系统")
CONFIG_FIELD(bool,  bAutoLoadConfigOnStartup, true)

CONFIG_GROUP("界面")
CONFIG_FIELD(int,   InjectionMode,         0)      // 0 = SwapHook, 1 = Overlay
CONFIG_FIELD(int,   RenderBackend,         1)      // 0 = OpenGL, 1 = Vulkan
CONFIG_FIELD(bool,  bShowImGuiDraw,        true)
CONFIG_FIELD(float, FontScale,             1.0f)

#ifdef _CONFIG_FIELDS_INL_SELFGUARD
#undef CONFIG_GROUP
#undef CONFIG_FIELD
#undef CONFIG_ARR
#undef CONFIG_NOSAVE
#undef CONFIG_NOSAVE_ARR
#undef _CONFIG_FIELDS_INL_SELFGUARD
#endif
