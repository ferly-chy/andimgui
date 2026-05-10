#include "SwapChainHook.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include "AndroidPlatform/AndroidPlatform.h"
#include "Core/ElfScannerManager.h"
#include "Core/HookLifecycleManager.h"
#include "Core/ResourceManager.h"
#include "Dobby/dobby.h"
#include "ImGuiExtern/ImGuiSoftKeyboard.h"
#include "Utils/Logger.h"
#include "imgui/backends/imgui_impl_android.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui.h"

// ===========================================================================
// 调试日志开关：设为 0 关闭 SwapChainHook 的详细调试日志（LOGE 始终保留）
// ===========================================================================
#define SWAPCHAIN_DEBUG_LOG 1

#if SWAPCHAIN_DEBUG_LOG
#define SCH_LOGI(fmt, ...) LOGI(fmt, ##__VA_ARGS__)
#define SCH_LOGW(fmt, ...) LOGW(fmt, ##__VA_ARGS__)
#else
#define SCH_LOGI(fmt, ...)                                                     \
  do {                                                                         \
  } while (0)
#define SCH_LOGW(fmt, ...)                                                     \
  do {                                                                         \
  } while (0)
#endif

// ===========================================================================
// 内部状态
// ===========================================================================

static std::atomic<bool> g_Installed{false};
static std::atomic<bool> g_ImGuiReady{false};
static std::mutex g_CallbackMutex;
static std::function<void()> g_RenderCallback;
static int g_Width = 0;
static int g_Height = 0;
static bool g_VkInitialized = false;

// ---------------------------------------------------------------------------
// OpenGL ES 路径
// ---------------------------------------------------------------------------

static EGLBoolean (*g_OrigEglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC g_OrigEglSwapBuffersWithDamage =
    nullptr;
static bool g_GlesInitialized = false;
static ANativeWindow *g_ImGuiWindow = nullptr;
static EGLContext g_ImGuiEglContext = EGL_NO_CONTEXT;

static std::function<void()> GetRenderCallbackSnapshot() {
  std::lock_guard<std::mutex> lk(g_CallbackMutex);
  return g_RenderCallback;
}

static void DestroyCurrentImGuiContext(bool shutdownGlesBackend,
                                       bool shutdownVulkanBackend) {
  if (!ImGui::GetCurrentContext())
    return;

  g_ImGuiReady.store(false);
  if (shutdownVulkanBackend)
    ImGui_ImplVulkan_Shutdown();
  if (shutdownGlesBackend)
    ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplAndroid_Shutdown();
  ResourceManager::GetInstance().reset();
  ImGui::DestroyContext();
}

struct GlStateBackup {
  GLint activeTexture = 0;
  GLint program = 0;
  GLint texture2D = 0;
  GLint sampler = 0;
  GLint arrayBuffer = 0;
  GLint vertexArray = 0;
  GLint framebuffer = 0;
  GLint viewport[4]{};
  GLint scissorBox[4]{};
  GLint blendSrcRgb = 0;
  GLint blendDstRgb = 0;
  GLint blendSrcAlpha = 0;
  GLint blendDstAlpha = 0;
  GLint blendEquationRgb = 0;
  GLint blendEquationAlpha = 0;
  GLboolean blend = GL_FALSE;
  GLboolean cullFace = GL_FALSE;
  GLboolean depthTest = GL_FALSE;
  GLboolean stencilTest = GL_FALSE;
  GLboolean scissorTest = GL_FALSE;
  GLboolean colorMask[4]{};
};

static GlStateBackup BackupGlState() {
  GlStateBackup state{};
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture2D);
#ifdef GL_SAMPLER_BINDING
  glGetIntegerv(GL_SAMPLER_BINDING, &state.sampler);
#endif
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
#ifdef GL_VERTEX_ARRAY_BINDING
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertexArray);
#endif
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.framebuffer);
  glGetIntegerv(GL_VIEWPORT, state.viewport);
  glGetIntegerv(GL_SCISSOR_BOX, state.scissorBox);
  glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
  state.blend = glIsEnabled(GL_BLEND);
  state.cullFace = glIsEnabled(GL_CULL_FACE);
  state.depthTest = glIsEnabled(GL_DEPTH_TEST);
  state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
  state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
  glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask);
  return state;
}

static void RestoreGlState(const GlStateBackup& state) {
  glUseProgram(state.program);
#ifdef GL_SAMPLER_BINDING
  glBindSampler(0, state.sampler);
#endif
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, state.texture2D);
#ifdef GL_VERTEX_ARRAY_BINDING
  glBindVertexArray(state.vertexArray);
#endif
  glBindBuffer(GL_ARRAY_BUFFER, state.arrayBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
  glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
  glScissor(state.scissorBox[0], state.scissorBox[1], state.scissorBox[2], state.scissorBox[3]);
  glBlendEquationSeparate(state.blendEquationRgb, state.blendEquationAlpha);
  glBlendFuncSeparate(state.blendSrcRgb, state.blendDstRgb, state.blendSrcAlpha, state.blendDstAlpha);
  if (state.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (state.cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
  if (state.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (state.stencilTest) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
  if (state.scissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
  glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
  glActiveTexture(state.activeTexture);
}

/**
 * @brief 在游戏的 EGL Context 上初始化 ImGui（仅首次调用）
 */
static bool InitImGuiOnGameContext(EGLDisplay display, EGLSurface surface) {
  // 获取 Surface 尺寸
  EGLint w = 0, h = 0;
  eglQuerySurface(display, surface, EGL_WIDTH, &w);
  eglQuerySurface(display, surface, EGL_HEIGHT, &h);
  SCH_LOGI("[SwapChainHook] eglQuerySurface returned %dx%d", w, h);

  // 回退：从 ANativeWindow 获取尺寸
  ANativeWindow *nativeWindow = AndroidPlatform::GetNativeWindow();
  if ((w <= 0 || h <= 0) && nativeWindow) {
    w = ANativeWindow_getWidth(nativeWindow);
    h = ANativeWindow_getHeight(nativeWindow);
    SCH_LOGI("[SwapChainHook] Fallback to ANativeWindow size %dx%d", w, h);
  }

  if (w <= 0 || h <= 0) {
    LOGE("[SwapChainHook] Invalid surface size %dx%d", w, h);
    return false;
  }
  g_Width = w;
  g_Height = h;

  // 创建 ImGui 上下文
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2((float)w, (float)h);
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  style.WindowPadding = ImVec2(12.0f, 12.0f);
  style.FramePadding = ImVec2(8.0f, 6.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.TouchExtraPadding = ImVec2(4.0f, 4.0f);
  style.ScrollbarSize = 20.0f;
  style.GrabMinSize = 12.0f;

  style.WindowRounding = 10.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 5.0f;
  style.ScrollbarRounding = 12.0f;
  style.GrabRounding = 8.0f;
  style.TabRounding = 8.0f;

  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.AntiAliasedLines = true;
  style.AntiAliasedFill = true;
  style.ScaleAllSizes(2.0f);

  colors[ImGuiCol_Text] = ImVec4(0.80f, 0.84f, 0.96f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.45f, 0.55f, 1.00f);

  colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.11f, 0.96f);

  colors[ImGuiCol_Border] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.26f, 0.35f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);

  colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.07f, 0.11f, 1.00f);

  colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.37f, 0.38f, 0.51f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.45f, 0.55f, 1.00f);

  colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.75f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.78f, 0.93f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.78f, 0.93f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.80f, 0.65f, 0.97f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.55f, 0.87f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.26f, 0.35f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);

  colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);

  colors[ImGuiCol_PlotLines] = ImVec4(0.94f, 0.72f, 0.42f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.71f, 0.75f, 1.00f, 1.00f);

  // 平台后端
  ANativeWindow *initWindow = AndroidPlatform::GetNativeWindow();
  if (!initWindow) {
    LOGE("[SwapChainHook] ANativeWindow is null, cannot init ImGui");
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }
  ImGui_ImplAndroid_Init(initWindow);

  // OpenGL 渲染后端（复用游戏 Context）
  if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
    LOGE("[SwapChainHook] ImGui_ImplOpenGL3_Init failed");
    ImGui_ImplAndroid_Shutdown();
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }

  // 字体
  if (!ResourceManager::GetInstance().initializeFonts(30.0f)) {
    LOGE("[SwapChainHook] Font initialization failed");
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }

  g_GlesInitialized = true;
  g_ImGuiReady.store(true);
  g_ImGuiWindow = initWindow;
  g_ImGuiEglContext = eglGetCurrentContext();

  SCH_LOGI("[SwapChainHook] ImGui initialized on game EGL context=%p  %dx%d",
           g_ImGuiEglContext, w, h);
  return true;
}

/**
 * @brief eglSwapBuffers hook
 *
 * 在游戏调用 eglSwapBuffers 提交帧之前：
 *   1. 保存完整 GL 状态
 *   2. 渲染 ImGui 到游戏的默认帧缓冲区（FBO 0）
 *   3. 恢复 GL 状态
 *   4. 调用原始 eglSwapBuffers
 */
static EGLBoolean Hooked_eglSwapBuffers(EGLDisplay display,
                                        EGLSurface surface) {
  if (eglGetCurrentContext() == EGL_NO_CONTEXT)
    return g_OrigEglSwapBuffers(display, surface);

  // Vulkan 已接管渲染，不再在 EGL 上绘制 ImGui
  if (g_VkInitialized)
    return g_OrigEglSwapBuffers(display, surface);

  if (!g_GlesInitialized) {
    if (!InitImGuiOnGameContext(display, surface))
      return g_OrigEglSwapBuffers(display, surface);
  }

  ANativeWindow *currentWindow = AndroidPlatform::GetNativeWindow();
  if (!currentWindow)
    return g_OrigEglSwapBuffers(display, surface);

  // ANativeWindow 变化后需重新初始化 ImGui
  if (g_GlesInitialized && currentWindow != g_ImGuiWindow) {
    SCH_LOGI("[SwapChainHook] Window changed: %p->%p, reinitializing ImGui",
             g_ImGuiWindow, currentWindow);
    g_ImGuiReady.store(false);
    DestroyCurrentImGuiContext(true, false);
    g_GlesInitialized = false;
    g_ImGuiWindow = nullptr;
    g_ImGuiEglContext = EGL_NO_CONTEXT;
    AndroidPlatform::ResetNativeWindowCache();
    if (!InitImGuiOnGameContext(display, surface))
      return g_OrigEglSwapBuffers(display, surface);
  }

  // --- 查询 EGL surface 实际尺寸 ---
  EGLint sw = 0, sh = 0;
  eglQuerySurface(display, surface, EGL_WIDTH, &sw);
  eglQuerySurface(display, surface, EGL_HEIGHT, &sh);

  // --- ImGui 渲染帧 ---
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame();

  // 通过 DisplayFramebufferScale 计算 ImGui 渲染后端真实帧缓冲分辨率。
  {
    ImGuiIO &ioUpdate = ImGui::GetIO();
    // DisplaySize 保持 ANativeWindow 尺寸（与触摸坐标一致）
    float dispW = ioUpdate.DisplaySize.x;
    float dispH = ioUpdate.DisplaySize.y;
    g_Width = (int)dispW;
    g_Height = (int)dispH;
    // 设置 framebuffer scale: EGL surface 尺寸 / 显示尺寸
    if (sw > 0 && sh > 0 && dispW > 0 && dispH > 0) {
      ioUpdate.DisplayFramebufferScale =
          ImVec2((float)sw / dispW, (float)sh / dispH);
    }
  }

  ImGui::NewFrame();
  ImGuiSoftKeyboard::PreUpdate();

  if (auto callback = GetRenderCallbackSnapshot())
    callback();

  ImGuiSoftKeyboard::Draw();
  ImGui::Render();

  // --- 保存游戏的 GL 状态 ---
  GlStateBackup glState = BackupGlState();

  // 渲染到游戏的默认帧缓冲区（FBO 0）
  // 注意：不做 glClear，保留游戏已有画面内容
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // viewport 使用 EGL surface 实际分辨率（framebuffer 尺寸）
  ImGuiIO &io = ImGui::GetIO();
  int fbW = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  int fbH = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  glViewport(0, 0, fbW, fbH);

  // 开启混合，ImGui 绘制叠加在游戏画面之上
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // --- 恢复游戏的 GL 状态 ---
  RestoreGlState(glState);

  // --- 调用原始 eglSwapBuffers ---
  return g_OrigEglSwapBuffers(display, surface);
}

/**
 * @brief eglSwapBuffersWithDamageKHR hook
 */
static EGLBoolean Hooked_eglSwapBuffersWithDamageKHR(EGLDisplay display,
                                                     EGLSurface surface,
                                                     EGLint *rects,
                                                     EGLint n_rects) {
  if (eglGetCurrentContext() == EGL_NO_CONTEXT)
    return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

  // Vulkan 已接管渲染，不再在 EGL 上绘制 ImGui
  if (g_VkInitialized)
    return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

  if (!g_GlesInitialized) {
    if (!InitImGuiOnGameContext(display, surface))
      return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
  }

  ANativeWindow *currentWindow = AndroidPlatform::GetNativeWindow();
  if (!currentWindow)
    return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);

  if (g_GlesInitialized && currentWindow != g_ImGuiWindow) {
    SCH_LOGI("[SwapChainHook] WithDamage: Window changed: %p->%p, "
             "reinitializing ImGui",
             g_ImGuiWindow, currentWindow);
    g_ImGuiReady.store(false);
    DestroyCurrentImGuiContext(true, false);
    g_GlesInitialized = false;
    g_ImGuiWindow = nullptr;
    g_ImGuiEglContext = EGL_NO_CONTEXT;
    AndroidPlatform::ResetNativeWindowCache();
    if (!InitImGuiOnGameContext(display, surface))
      return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
  }

  EGLint sw = 0, sh = 0;
  eglQuerySurface(display, surface, EGL_WIDTH, &sw);
  eglQuerySurface(display, surface, EGL_HEIGHT, &sh);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame();

  {
    ImGuiIO &ioUpdate = ImGui::GetIO();
    float dispW = ioUpdate.DisplaySize.x;
    float dispH = ioUpdate.DisplaySize.y;
    g_Width = (int)dispW;
    g_Height = (int)dispH;
    if (sw > 0 && sh > 0 && dispW > 0 && dispH > 0)
      ioUpdate.DisplayFramebufferScale =
          ImVec2((float)sw / dispW, (float)sh / dispH);
  }

  ImGui::NewFrame();
  ImGuiSoftKeyboard::PreUpdate();

  if (auto callback = GetRenderCallbackSnapshot())
    callback();

  ImGuiSoftKeyboard::Draw();
  ImGui::Render();

  // --- 保存游戏的 GL 状态 ---
  GlStateBackup glState = BackupGlState();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  ImGuiIO &io = ImGui::GetIO();
  int fbW = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  int fbH = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  glViewport(0, 0, fbW, fbH);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  // --- 恢复游戏的 GL 状态 ---
  RestoreGlState(glState);

  return g_OrigEglSwapBuffersWithDamage(display, surface, rects, n_rects);
}

// ---------------------------------------------------------------------------
// Vulkan 路径
// ---------------------------------------------------------------------------

// Vulkan 函数指针
static PFN_vkQueuePresentKHR g_OrigVkQueuePresent = nullptr;
static PFN_vkCreateDevice g_OrigVkCreateDevice = nullptr;
static PFN_vkDestroyDevice g_OrigVkDestroyDevice = nullptr;
static PFN_vkGetDeviceQueue g_OrigVkGetDeviceQueue = nullptr;
static PFN_vkCreateSwapchainKHR g_OrigVkCreateSwapchain = nullptr;
static PFN_vkDestroySwapchainKHR g_OrigVkDestroySwapchain = nullptr;

// 捕获的 Vulkan 对象
static VkPhysicalDevice g_VkPhysDev = VK_NULL_HANDLE;
static VkDevice g_VkDevice = VK_NULL_HANDLE;
static VkQueue g_VkQueue = VK_NULL_HANDLE;
static uint32_t g_VkQueueFamily = UINT32_MAX;

// Vulkan ImGui 渲染资源
static VkRenderPass g_VkRenderPass = VK_NULL_HANDLE;
static VkCommandPool g_VkCommandPool = VK_NULL_HANDLE;
static VkDescriptorPool g_VkDescriptorPool = VK_NULL_HANDLE;
static VkSwapchainKHR g_VkSwapchain = VK_NULL_HANDLE;

static std::vector<VkImage> g_VkSwapImages;
static std::vector<VkImageView> g_VkSwapImageViews;
static std::vector<VkFramebuffer> g_VkFramebuffers;
static std::vector<VkCommandBuffer> g_VkCommandBuffers;
static std::vector<VkFence> g_VkFences;
static std::vector<VkSemaphore> g_VkRenderCompleteSemaphores;

static VkFormat g_VkSwapFormat = VK_FORMAT_UNDEFINED;
static VkExtent2D g_VkSwapExtent = {}; // 交换链实际尺寸（可能是竖屏）
static VkSurfaceTransformFlagBitsKHR g_VkPreTransform =
    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

/**
 * @brief 判断 preTransform 是否包含 90°/270° 旋转
 */
static bool IsRotated90or270(VkSurfaceTransformFlagBitsKHR transform) {
  return (transform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
                       VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) != 0;
}

/**
 * @brief 旋转 ImGui DrawData 的顶点和裁剪矩形
 *
 * 游戏使用 preTransform 旋转时，交换链图像是竖屏方向，
 * 但 ImGui 按横屏布局生成顶点。需要将横屏坐标映射到竖屏帧缓冲区。
 *
 * ROTATE_90 (90° CW):
 *   横屏 (x, y) → 竖屏 (dispH - y, x)
 *   裁剪矩形 (x1,y1,x2,y2) → (dispH - y2, x1, dispH - y1, x2)
 *
 * ROTATE_270 (270° CW = 90° CCW):
 *   横屏 (x, y) → 竖屏 (y, dispW - x)
 *   裁剪矩形 (x1,y1,x2,y2) → (y1, dispW - x2, y2, dispW - x1)
 *
 * 注意：旋转使用 DisplaySize（顶点实际坐标范围），而非 fbExtent（交换链像素）。
 * 旋转后通过 FramebufferScale 将 rotated-display 坐标映射到交换链帧缓冲像素。
 */
static void RotateImDrawData(ImDrawData *drawData,
                             VkSurfaceTransformFlagBitsKHR transform,
                             VkExtent2D fbExtent) {
  // 顶点坐标所在的 DisplaySize 空间
  float dispW = drawData->DisplaySize.x;
  float dispH = drawData->DisplaySize.y;

  bool is90 = (transform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) != 0;
  bool is270 = (transform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) != 0;

  if (!is90 && !is270)
    return;

  // 旋转所有顶点（使用 DisplaySize 坐标域）
  for (int n = 0; n < drawData->CmdListsCount; n++) {
    ImDrawList *cmdList = drawData->CmdLists[n];
    for (int i = 0; i < cmdList->VtxBuffer.Size; i++) {
      ImDrawVert &v = cmdList->VtxBuffer[i];
      float x = v.pos.x;
      float y = v.pos.y;
      if (is90) {
        v.pos.x = dispH - y;
        v.pos.y = x;
      } else // is270
      {
        v.pos.x = y;
        v.pos.y = dispW - x;
      }
    }

    // 旋转裁剪/剪裁矩形
    for (int i = 0; i < cmdList->CmdBuffer.Size; i++) {
      ImDrawCmd &cmd = cmdList->CmdBuffer[i];
      float cx1 = cmd.ClipRect.x;
      float cy1 = cmd.ClipRect.y;
      float cx2 = cmd.ClipRect.z;
      float cy2 = cmd.ClipRect.w;
      if (is90) {
        cmd.ClipRect = ImVec4(dispH - cy2, cx1, dispH - cy1, cx2);
      } else // is270
      {
        cmd.ClipRect = ImVec4(cy1, dispW - cx2, cy2, dispW - cx1);
      }
    }
  }

  // 旋转后：顶点坐标范围从 (dispW × dispH) 变为 (dispH × dispW)
  // 通过 FramebufferScale 将 rotated-display 坐标映射到实际交换链帧缓冲像素
  float rotW = dispH; // 旋转后的显示宽度
  float rotH = dispW; // 旋转后的显示高度
  float fbW = (float)fbExtent.width;
  float fbH = (float)fbExtent.height;

  drawData->DisplaySize = ImVec2(rotW, rotH);
  drawData->DisplayPos = ImVec2(0, 0);
  drawData->FramebufferScale = ImVec2(fbW / rotW, fbH / rotH);
}

/**
 * @brief 清理 Vulkan ImGui 渲染资源
 */
static void CleanupVkResources() {
  if (g_VkDevice == VK_NULL_HANDLE)
    return;

  vkDeviceWaitIdle(g_VkDevice);

  if (g_VkInitialized)
    ImGui_ImplVulkan_Shutdown();

  for (auto semaphore : g_VkRenderCompleteSemaphores)
    if (semaphore)
      vkDestroySemaphore(g_VkDevice, semaphore, nullptr);
  g_VkRenderCompleteSemaphores.clear();

  for (auto f : g_VkFences)
    if (f)
      vkDestroyFence(g_VkDevice, f, nullptr);
  g_VkFences.clear();

  g_VkCommandBuffers.clear();

  for (auto fb : g_VkFramebuffers)
    if (fb)
      vkDestroyFramebuffer(g_VkDevice, fb, nullptr);
  g_VkFramebuffers.clear();

  for (auto iv : g_VkSwapImageViews)
    if (iv)
      vkDestroyImageView(g_VkDevice, iv, nullptr);
  g_VkSwapImageViews.clear();

  g_VkSwapImages.clear();

  if (g_VkRenderPass) {
    vkDestroyRenderPass(g_VkDevice, g_VkRenderPass, nullptr);
    g_VkRenderPass = VK_NULL_HANDLE;
  }
  if (g_VkCommandPool) {
    vkDestroyCommandPool(g_VkDevice, g_VkCommandPool, nullptr);
    g_VkCommandPool = VK_NULL_HANDLE;
  }
  if (g_VkDescriptorPool) {
    vkDestroyDescriptorPool(g_VkDevice, g_VkDescriptorPool, nullptr);
    g_VkDescriptorPool = VK_NULL_HANDLE;
  }

  g_VkInitialized = false;
}

/**
 * @brief 为 Vulkan 交换链创建 ImGui 资源（RenderPass、Framebuffer 等）
 *
 * RenderPass 使用 LOAD_OP_LOAD 保留游戏原有画面，ImGui 叠加渲染其上。
 */
static bool CreateVkImGuiResources() {
  if (g_VkDevice == VK_NULL_HANDLE || g_VkSwapImages.empty())
    return false;

  // -- RenderPass: 保留游戏画面（LOAD_OP_LOAD），ImGui 叠加 --
  VkAttachmentDescription attachment{};
  attachment.format = g_VkSwapFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // 保留游戏帧
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = 0;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &attachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dep;

  if (vkCreateRenderPass(g_VkDevice, &rpInfo, nullptr, &g_VkRenderPass) !=
      VK_SUCCESS) {
    LOGE("[SwapChainHook/Vk] vkCreateRenderPass failed");
    return false;
  }

  // -- ImageView + Framebuffer --
  uint32_t imageCount = (uint32_t)g_VkSwapImages.size();
  g_VkSwapImageViews.resize(imageCount);
  g_VkFramebuffers.resize(imageCount);

  for (uint32_t i = 0; i < imageCount; i++) {
    VkImageViewCreateInfo ivInfo{};
    ivInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivInfo.image = g_VkSwapImages[i];
    ivInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivInfo.format = g_VkSwapFormat;
    ivInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivInfo.subresourceRange.baseMipLevel = 0;
    ivInfo.subresourceRange.levelCount = 1;
    ivInfo.subresourceRange.baseArrayLayer = 0;
    ivInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(g_VkDevice, &ivInfo, nullptr,
                          &g_VkSwapImageViews[i]) != VK_SUCCESS) {
      LOGE("[SwapChainHook/Vk] vkCreateImageView[%u] failed", i);
      return false;
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = g_VkRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &g_VkSwapImageViews[i];
    fbInfo.width = g_VkSwapExtent.width;
    fbInfo.height = g_VkSwapExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(g_VkDevice, &fbInfo, nullptr,
                            &g_VkFramebuffers[i]) != VK_SUCCESS) {
      LOGE("[SwapChainHook/Vk] vkCreateFramebuffer[%u] failed", i);
      return false;
    }
  }

  // -- CommandPool + CommandBuffers --
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = g_VkQueueFamily;

  if (vkCreateCommandPool(g_VkDevice, &poolInfo, nullptr, &g_VkCommandPool) !=
      VK_SUCCESS) {
    LOGE("[SwapChainHook/Vk] vkCreateCommandPool failed");
    return false;
  }

  g_VkCommandBuffers.resize(imageCount);
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = g_VkCommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = imageCount;

  if (vkAllocateCommandBuffers(g_VkDevice, &allocInfo,
                               g_VkCommandBuffers.data()) != VK_SUCCESS) {
    LOGE("[SwapChainHook/Vk] vkAllocateCommandBuffers failed");
    return false;
  }

  // -- Fence（每张图像一个，用于等待上一次该 cmd 完成） --
  g_VkFences.resize(imageCount);
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < imageCount; i++) {
    if (vkCreateFence(g_VkDevice, &fenceInfo, nullptr, &g_VkFences[i]) !=
        VK_SUCCESS) {
      LOGE("[SwapChainHook/Vk] vkCreateFence[%u] failed", i);
      return false;
    }
  }

  g_VkRenderCompleteSemaphores.resize(imageCount);
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (uint32_t i = 0; i < imageCount; i++) {
    if (vkCreateSemaphore(g_VkDevice, &semaphoreInfo, nullptr,
                          &g_VkRenderCompleteSemaphores[i]) != VK_SUCCESS) {
      LOGE("[SwapChainHook/Vk] vkCreateSemaphore[%u] failed", i);
      return false;
    }
  }

  // -- DescriptorPool（ImGui 字体纹理等） --
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
  };
  VkDescriptorPoolCreateInfo dpInfo{};
  dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dpInfo.maxSets = 16;
  dpInfo.poolSizeCount = 1;
  dpInfo.pPoolSizes = poolSizes;

  if (vkCreateDescriptorPool(g_VkDevice, &dpInfo, nullptr,
                             &g_VkDescriptorPool) != VK_SUCCESS) {
    LOGE("[SwapChainHook/Vk] vkCreateDescriptorPool failed");
    return false;
  }

  SCH_LOGI("[SwapChainHook/Vk] Resources created  images=%u  %ux%u", imageCount,
           g_VkSwapExtent.width, g_VkSwapExtent.height);
  return true;
}

/**
 * @brief 初始化 ImGui Vulkan 后端
 */
static bool InitImGuiVulkan() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  // 确定逻辑显示尺寸（始终为横屏）
  // 当 preTransform 包含 90°/270° 旋转时，imageExtent 是竖屏尺寸，需要交换
  uint32_t displayW = g_VkSwapExtent.width;
  uint32_t displayH = g_VkSwapExtent.height;
  if (IsRotated90or270(g_VkPreTransform)) {
    std::swap(displayW, displayH);
    SCH_LOGI(
        "[SwapChainHook/Vk] preTransform=0x%x, swapped display %ux%u → %ux%u",
        (int)g_VkPreTransform, g_VkSwapExtent.width, g_VkSwapExtent.height,
        displayW, displayH);
  }

  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2((float)displayW, (float)displayH);
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  style.WindowPadding = ImVec2(12.0f, 12.0f);
  style.FramePadding = ImVec2(8.0f, 6.0f);
  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.TouchExtraPadding = ImVec2(4.0f, 4.0f);
  style.ScrollbarSize = 20.0f;
  style.GrabMinSize = 12.0f;

  style.WindowRounding = 10.0f;
  style.FrameRounding = 5.0f;
  style.PopupRounding = 5.0f;
  style.ScrollbarRounding = 12.0f;
  style.GrabRounding = 8.0f;
  style.TabRounding = 8.0f;

  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;
  style.AntiAliasedLines = true;
  style.AntiAliasedFill = true;
  style.ScaleAllSizes(2.0f);

  colors[ImGuiCol_Text] = ImVec4(0.80f, 0.84f, 0.96f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.42f, 0.45f, 0.55f, 1.00f);

  colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.11f, 0.96f);

  colors[ImGuiCol_Border] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

  colors[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.26f, 0.35f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);

  colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.07f, 0.11f, 1.00f);

  colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.37f, 0.38f, 0.51f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42f, 0.45f, 0.55f, 1.00f);

  colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.75f, 1.00f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.78f, 0.93f, 1.00f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.78f, 0.93f, 1.00f);
  colors[ImGuiCol_Button] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.80f, 0.65f, 0.97f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.55f, 0.87f, 1.00f);
  colors[ImGuiCol_Header] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.26f, 0.35f, 1.00f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);

  colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);
  colors[ImGuiCol_TabHovered] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_TabActive] = ImVec4(0.19f, 0.20f, 0.27f, 1.00f);
  colors[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.09f, 0.15f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.12f, 0.18f, 1.00f);

  colors[ImGuiCol_PlotLines] = ImVec4(0.94f, 0.72f, 0.42f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.31f, 0.32f, 0.42f, 1.00f);
  colors[ImGuiCol_NavHighlight] = ImVec4(0.71f, 0.75f, 1.00f, 1.00f);

  // 平台后端
  ANativeWindow *vkWindow = AndroidPlatform::GetNativeWindow();
  if (!vkWindow) {
    LOGE("[SwapChainHook/Vk] ANativeWindow is null, cannot init ImGui");
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }
  ImGui_ImplAndroid_Init(vkWindow);

  // Vulkan 渲染后端
  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = VK_NULL_HANDLE; // 不需要，ImGui 不自行创建资源
  initInfo.PhysicalDevice = g_VkPhysDev;
  initInfo.Device = g_VkDevice;
  initInfo.QueueFamily = g_VkQueueFamily;
  initInfo.Queue = g_VkQueue;
  initInfo.DescriptorPool = g_VkDescriptorPool;
  initInfo.PipelineInfoMain.RenderPass = g_VkRenderPass;
  initInfo.MinImageCount = (uint32_t)g_VkSwapImages.size();
  initInfo.ImageCount = (uint32_t)g_VkSwapImages.size();
  initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  initInfo.CheckVkResultFn = [](VkResult r) {
    if (r != VK_SUCCESS)
      LOGE("[SwapChainHook/Vk] ImGui VkResult=%d", (int)r);
  };

  if (!ImGui_ImplVulkan_Init(&initInfo)) {
    LOGE("[SwapChainHook/Vk] ImGui_ImplVulkan_Init failed");
    ImGui_ImplAndroid_Shutdown();
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }

  // 字体
  if (!ResourceManager::GetInstance().initializeFonts(30.0f)) {
    LOGE("[SwapChainHook/Vk] Font initialization failed");
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ResourceManager::GetInstance().reset();
    ImGui::DestroyContext();
    return false;
  }

  // 对外报告横屏尺寸
  g_Width = (int)displayW;
  g_Height = (int)displayH;
  g_VkInitialized = true;
  g_ImGuiReady.store(true);

  SCH_LOGI("[SwapChainHook/Vk] ImGui initialized  display=%ux%u  fb=%ux%u  "
           "preTransform=0x%x",
           displayW, displayH, g_VkSwapExtent.width, g_VkSwapExtent.height,
           (int)g_VkPreTransform);
  return true;
}

// ---------------------------------------------------------------------------
// Vulkan Hooks
// ---------------------------------------------------------------------------

/**
 * @brief vkCreateDevice hook — 捕获 VkPhysicalDevice / VkDevice / 队列族
 */
static VkResult VKAPI_CALL Hooked_vkCreateDevice(
    VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
  VkResult result =
      g_OrigVkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
  if (result == VK_SUCCESS && pDevice) {
    g_VkPhysDev = physicalDevice;
    g_VkDevice = *pDevice;

    // 从 DeviceCreateInfo 获取可用图形队列族索引
    g_VkQueueFamily = UINT32_MAX;
    if (pCreateInfo && pCreateInfo->queueCreateInfoCount > 0) {
      uint32_t qfCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
      std::vector<VkQueueFamilyProperties> qfProps(qfCount);
      if (qfCount > 0)
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, qfProps.data());

      for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i) {
        uint32_t family = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
        if (family < qfProps.size() && (qfProps[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
          g_VkQueueFamily = family;
          break;
        }
      }

      if (g_VkQueueFamily == UINT32_MAX)
        g_VkQueueFamily = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
    }

    SCH_LOGI(
        "[SwapChainHook/Vk] Captured device=%p  physDev=%p  queueFamily=%u",
        g_VkDevice, g_VkPhysDev, g_VkQueueFamily);
  }
  return result;
}

/**
 * @brief vkGetDeviceQueue hook — 捕获图形队列
 */
static void VKAPI_CALL Hooked_vkGetDeviceQueue(VkDevice device,
                                               uint32_t queueFamilyIndex,
                                               uint32_t queueIndex,
                                               VkQueue *pQueue) {
  g_OrigVkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
  if (pQueue && *pQueue && queueFamilyIndex == g_VkQueueFamily) {
    g_VkQueue = *pQueue;
    SCH_LOGI("[SwapChainHook/Vk] Captured queue=%p  family=%u", g_VkQueue,
             queueFamilyIndex);
  }
}

/**
 * @brief vkCreateSwapchainKHR hook — 捕获交换链信息并创建渲染资源
 */
static VkResult VKAPI_CALL Hooked_vkCreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
  // 先清理旧资源（交换链重建时，或 EGL→Vulkan 切换时）
  if (g_VkInitialized) {
    CleanupVkResources();
    DestroyCurrentImGuiContext(false, false);
  } else if (g_GlesInitialized) {
    // 游戏从 EGL 切换到 Vulkan，先清理 EGL ImGui
    SCH_LOGI("[SwapChainHook/Vk] EGL→Vulkan transition, cleaning up EGL ImGui");
    g_ImGuiReady.store(false);
    DestroyCurrentImGuiContext(true, false);
    g_GlesInitialized = false;
    g_ImGuiWindow = nullptr;
    g_ImGuiEglContext = EGL_NO_CONTEXT;
    AndroidPlatform::ResetNativeWindowCache();
  }

  VkResult result =
      g_OrigVkCreateSwapchain(device, pCreateInfo, pAllocator, pSwapchain);
  if (result != VK_SUCCESS || !pSwapchain)
    return result;

  g_VkSwapchain = *pSwapchain;
  g_VkSwapFormat = pCreateInfo->imageFormat;
  g_VkSwapExtent = pCreateInfo->imageExtent;
  g_VkPreTransform = (VkSurfaceTransformFlagBitsKHR)pCreateInfo->preTransform;

  // 获取交换链图像
  uint32_t imageCount = 0;
  vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount, nullptr);
  g_VkSwapImages.resize(imageCount);
  vkGetSwapchainImagesKHR(device, *pSwapchain, &imageCount,
                          g_VkSwapImages.data());

  SCH_LOGI("[SwapChainHook/Vk] Swapchain created  %ux%u  images=%u  format=%d",
           g_VkSwapExtent.width, g_VkSwapExtent.height, imageCount,
           (int)g_VkSwapFormat);

  // 创建 ImGui 渲染资源
  if (g_VkDevice != VK_NULL_HANDLE && g_VkQueue != VK_NULL_HANDLE) {
    if (CreateVkImGuiResources())
      InitImGuiVulkan();
  }

  return result;
}

/**
 * @brief vkDestroySwapchainKHR hook — 清理渲染资源
 */
static void VKAPI_CALL
Hooked_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                             const VkAllocationCallbacks *pAllocator) {
  if (swapchain == g_VkSwapchain && g_VkInitialized) {
    CleanupVkResources();
    DestroyCurrentImGuiContext(false, false);
    g_ImGuiReady.store(false);
  }
  g_OrigVkDestroySwapchain(device, swapchain, pAllocator);
}

/**
 * @brief vkDestroyDevice hook — 完全清理
 */
static void VKAPI_CALL Hooked_vkDestroyDevice(
    VkDevice device, const VkAllocationCallbacks *pAllocator) {
  if (device == g_VkDevice) {
    CleanupVkResources();
    DestroyCurrentImGuiContext(false, false);
    g_ImGuiReady.store(false);
    g_VkDevice = VK_NULL_HANDLE;
    g_VkQueue = VK_NULL_HANDLE;
  }
  g_OrigVkDestroyDevice(device, pAllocator);
}

/**
 * @brief vkQueuePresentKHR hook — ImGui 渲染注入点
 *
 * 在游戏调用 present 前：
 *   1. 等待该图像对应的 fence
 *   2. 录制 ImGui 渲染命令到命令缓冲
 *   3. 提交命令缓冲（等待游戏的 renderFinished 信号量）
 *   4. 调用原始 vkQueuePresentKHR
 */
static VkResult VKAPI_CALL
Hooked_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
  if (!pPresentInfo || pPresentInfo->swapchainCount == 0)
    return g_OrigVkQueuePresent(queue, pPresentInfo);

  if (!g_VkInitialized)
    return g_OrigVkQueuePresent(queue, pPresentInfo);

  // 找到我们跟踪的交换链
  for (uint32_t sc = 0; sc < pPresentInfo->swapchainCount; sc++) {
    if (pPresentInfo->pSwapchains[sc] != g_VkSwapchain)
      continue;

    uint32_t imageIndex = pPresentInfo->pImageIndices[sc];
    if (imageIndex >= g_VkCommandBuffers.size() ||
        imageIndex >= g_VkRenderCompleteSemaphores.size())
      continue;

    // 等待上一次使用该命令缓冲的 GPU 工作完成
    vkWaitForFences(g_VkDevice, 1, &g_VkFences[imageIndex], VK_TRUE,
                    UINT64_MAX);
    vkResetFences(g_VkDevice, 1, &g_VkFences[imageIndex]);

    // --- ImGui 新帧 ---
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplAndroid_NewFrame();

    {
      ImGuiIO &io = ImGui::GetIO();
      g_Width = (int)io.DisplaySize.x;
      g_Height = (int)io.DisplaySize.y;
    }

    ImGui::NewFrame();
    ImGuiSoftKeyboard::PreUpdate();

    if (auto callback = GetRenderCallbackSnapshot())
      callback();

    ImGuiSoftKeyboard::Draw();

    ImGui::Render();

    // --- 旋转 ImGui 绘制数据以匹配帧缓冲区方向 ---
    if (IsRotated90or270(g_VkPreTransform))
      RotateImDrawData(ImGui::GetDrawData(), g_VkPreTransform, g_VkSwapExtent);

    // --- 录制命令缓冲 ---
    VkCommandBuffer cmd = g_VkCommandBuffers[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = g_VkRenderPass;
    rpBegin.framebuffer = g_VkFramebuffers[imageIndex];
    rpBegin.renderArea.extent = g_VkSwapExtent;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    // --- 提交：等待游戏的 present 信号量 ---
    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VkSemaphore renderComplete = g_VkRenderCompleteSemaphores[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderComplete;

    if (pPresentInfo->waitSemaphoreCount > 0 && pPresentInfo->pWaitSemaphores) {
      submitInfo.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
      submitInfo.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
      std::vector<VkPipelineStageFlags> waitStages(
          pPresentInfo->waitSemaphoreCount, waitStage);
      submitInfo.pWaitDstStageMask = waitStages.data();
    }

    VkResult submitResult = vkQueueSubmit(queue, 1, &submitInfo, g_VkFences[imageIndex]);
    if (submitResult != VK_SUCCESS) {
      LOGE("[SwapChainHook/Vk] vkQueueSubmit failed: %d", (int)submitResult);
      vkResetFences(g_VkDevice, 1, &g_VkFences[imageIndex]);
    } else {
      VkPresentInfoKHR patchedPresent = *pPresentInfo;
      patchedPresent.waitSemaphoreCount = 1;
      patchedPresent.pWaitSemaphores = &renderComplete;
      return g_OrigVkQueuePresent(queue, &patchedPresent);
    }

    break; // 仅处理第一个匹配的交换链
  }

  return g_OrigVkQueuePresent(queue, pPresentInfo);
}

// ===========================================================================
// 公开接口
// ===========================================================================

namespace SwapChainHook {

void Install() {
  if (g_Installed.exchange(true))
    return;

  SCH_LOGI("[SwapChainHook] Installing hooks...");

  // --- 1. 创建辅助 VkInstance + VkDevice ---
  VkInstance helperInst = VK_NULL_HANDLE;
  VkDevice helperDev = VK_NULL_HANDLE;
  VkPhysicalDevice helperPhysDev = VK_NULL_HANDLE;

  do {
    VkInstanceCreateInfo instCI{};
    instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    VkResult r = vkCreateInstance(&instCI, nullptr, &helperInst);
    SCH_LOGI("[SwapChainHook] helper vkCreateInstance=%d  inst=%p", (int)r,
             helperInst);
    if (r != VK_SUCCESS || !helperInst)
      break;

    uint32_t pdCount = 0;
    vkEnumeratePhysicalDevices(helperInst, &pdCount, nullptr);
    if (pdCount == 0) {
      LOGE("[SwapChainHook] No physical devices found");
      break;
    }

    std::vector<VkPhysicalDevice> pds(pdCount);
    vkEnumeratePhysicalDevices(helperInst, &pdCount, pds.data());
    helperPhysDev = pds[0];
    SCH_LOGI("[SwapChainHook] helper physDev=%p (%u total)", helperPhysDev,
             pdCount);

    // 查找图形队列族
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(helperPhysDev, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(helperPhysDev, &qfCount,
                                             qfProps.data());

    uint32_t gfxFamily = 0;
    for (uint32_t i = 0; i < qfCount; i++) {
      if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        gfxFamily = i;
        break;
      }
    }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qCI{};
    qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCI.queueFamilyIndex = gfxFamily;
    qCI.queueCount = 1;
    qCI.pQueuePriorities = &prio;

    // 启用 swapchain 扩展，否则 vkGetDeviceProcAddr("vkQueuePresentKHR") 返回
    // null
    const char *exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo devCI{};
    devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCI.queueCreateInfoCount = 1;
    devCI.pQueueCreateInfos = &qCI;
    devCI.enabledExtensionCount = 1;
    devCI.ppEnabledExtensionNames = exts;

    r = vkCreateDevice(helperPhysDev, &devCI, nullptr, &helperDev);
    SCH_LOGI("[SwapChainHook] helper vkCreateDevice=%d  dev=%p", (int)r,
             helperDev);
  } while (false);

  // --- 2. 通过 vkGetDeviceProcAddr 解析 dispatch target 并 hook ---
  if (helperDev) {
    struct VkHookEntry {
      const char *name;
      void *hookFunc;
      void **origSlot;
    };
    VkHookEntry devHooks[] = {
        {"vkQueuePresentKHR", (void *)Hooked_vkQueuePresentKHR,
         (void **)&g_OrigVkQueuePresent},
        {"vkCreateSwapchainKHR", (void *)Hooked_vkCreateSwapchainKHR,
         (void **)&g_OrigVkCreateSwapchain},
        {"vkDestroySwapchainKHR", (void *)Hooked_vkDestroySwapchainKHR,
         (void **)&g_OrigVkDestroySwapchain},
        {"vkDestroyDevice", (void *)Hooked_vkDestroyDevice,
         (void **)&g_OrigVkDestroyDevice},
        {"vkGetDeviceQueue", (void *)Hooked_vkGetDeviceQueue,
         (void **)&g_OrigVkGetDeviceQueue},
    };

    for (auto &h : devHooks) {
      void *target = (void *)vkGetDeviceProcAddr(helperDev, h.name);
      if (target) {
        if (!HookLifecycleManager::GetInstance().install(h.name, target, h.hookFunc, h.origSlot, "SwapChain"))
          LOGE("[SwapChainHook] Failed to install hook: %s", h.name);
      } else {
        LOGE("[SwapChainHook] %s: vkGetDeviceProcAddr returned null", h.name);
      }
    }

    // vkCreateDevice 是 instance-level 函数，用 vkGetInstanceProcAddr
    {
      void *target =
          (void *)vkGetInstanceProcAddr(helperInst, "vkCreateDevice");
      if (target) {
        if (!HookLifecycleManager::GetInstance().install(
            "vkCreateDevice", target, (void *)Hooked_vkCreateDevice,
            (void **)&g_OrigVkCreateDevice, "SwapChain"))
          LOGE("[SwapChainHook] Failed to install hook: vkCreateDevice");
      }
    }
  } else {
    LOGE("[SwapChainHook] Cannot create helper VkDevice, Vulkan hooks not "
         "installed");
  }

  // --- 3. 清理辅助设备 ---
  // DobbyHook 修改 .text 段的机器码，不依赖 VkDevice 生命周期
  // 注意：此时 Hooked_vkDestroyDevice 已安装，但 device != g_VkDevice，安全通过
  if (helperDev) {
    vkDestroyDevice(helperDev, nullptr);
    SCH_LOGI("[SwapChainHook] helper device destroyed");
  }
  if (helperInst) {
    vkDestroyInstance(helperInst, nullptr);
    SCH_LOGI("[SwapChainHook] helper instance destroyed");
  }

  // === EGL hooks ===
  {
    if (!Elf.egl().isValid() && !Elf.scanAsync({"libEGL.so"}))
      LOGE("[SwapChainHook] Failed to scan libEGL.so");

    void *sym = Elf.egl().findSymbol("eglSwapBuffers");
    if (sym) {
      if (!HookLifecycleManager::GetInstance().install(
          "eglSwapBuffers", sym, (void *)Hooked_eglSwapBuffers,
          (void **)&g_OrigEglSwapBuffers, "SwapChain"))
        LOGE("[SwapChainHook] Failed to install hook: eglSwapBuffers");
    }

    void *symDmg = Elf.egl().findSymbol("eglSwapBuffersWithDamageKHR");
    if (symDmg) {
      if (!HookLifecycleManager::GetInstance().install(
          "eglSwapBuffersWithDamageKHR", symDmg,
          (void *)Hooked_eglSwapBuffersWithDamageKHR,
          (void **)&g_OrigEglSwapBuffersWithDamage, "SwapChain"))
        LOGE("[SwapChainHook] Failed to install hook: eglSwapBuffersWithDamageKHR");
    }
  }

  SCH_LOGI("[SwapChainHook] Hooks installed");
}

void Uninstall() {
  if (!g_Installed.exchange(false))
    return;

  SCH_LOGI("[SwapChainHook] Uninstalling...");

  HookLifecycleManager::GetInstance().uninstallByOwner("SwapChain");

  g_OrigEglSwapBuffers = nullptr;
  g_OrigEglSwapBuffersWithDamage = nullptr;

  // 清理 ImGui
  if (g_VkInitialized)
    CleanupVkResources();

  DestroyCurrentImGuiContext(g_GlesInitialized, false);
  AndroidPlatform::ResetNativeWindowCache();

  g_GlesInitialized = false;
  g_VkInitialized = false;
  g_ImGuiReady.store(false);

  SCH_LOGI("[SwapChainHook] Uninstalled");
}

bool IsInitialized() { return g_ImGuiReady.load(); }

void SetRenderCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lk(g_CallbackMutex);
  g_RenderCallback = std::move(callback);
}

int GetWidth() { return g_Width; }
int GetHeight() { return g_Height; }

} // namespace SwapChainHook
