# AndSwapChainHook (AndImgui) Project Context

This project, internally named `AndSwapChainHook` (and often referred to as `andimgui`), is a high-performance Android-native internal overlay library. It is designed to be injected into Android applications to render a modern graphical user interface (ImGui) on top of the target application's screen.

## Project Overview

### Purpose
The primary goal is to provide a robust, cross-graphics-API (Vulkan & EGL/GLES) overlay system for Android. It is commonly used for debugging, modding, or adding external controls to mobile games and applications.

### Key Technologies
- **Language:** C++20 (requires modern compiler support).
- **Build System:** CMake with Android NDK (targeting `arm64-v8a`).
- **UI Framework:** [Dear ImGui](https://github.com/ocornut/imgui).
- **Hooking Engine:** [Dobby](https://github.com/jmpews/Dobby) for runtime instruction patching.
- **Memory Manipulation:** [KittyMemory](https://github.com/MJx0/KittyMemory) / [KittyMemoryEx](https://github.com/MJx0/KittyMemoryEx) for ELF scanning and memory patching.
- **Graphics APIs:** Vulkan, EGL, OpenGL ES 3.

### Architecture
1.  **Entry Point (`source/Library.cpp`):**
    - Triggered via `JNI_OnLoad` (for injectors like `AndKittyInjector`) or global constructor.
    - Spawns a background thread to prevent blocking the application's main thread during initialization.
2.  **Swap Chain Hooking (`source/SwapChain/`):**
    - **EGL/GLES:** Hooks `eglSwapBuffers` to inject rendering calls into the OpenGL pipeline. Meticulously handles GL state save/restore to prevent flickering or crashes in the host app.
    - **Vulkan:** Hooks `vkQueuePresentKHR` and related swapchain creation functions. Implements a full Vulkan render pass for the overlay and handles screen rotation (`preTransform`) by rotating the ImGui draw data.
3.  **Input Interception (`source/InputEvent/`):**
    - Hooks the internal Android `InputConsumer::consume` (in `libinput.so`) to intercept low-level `AInputEvent`s.
    - Bridges native events to ImGui's input handling system.
4.  **Platform Abstraction (`source/AndroidPlatform/`):**
    - Uses JNI reflection to find the host application's `ANativeWindow` and `JavaVM`, which is essential for ImGui initialization and platform-specific logic.
5.  **ELF Scanning (`source/Core/`):**
    - `ElfScannerManager` uses X-macros to maintain a registry of critical libraries (`libc.so`, `libvulkan.so`, `libart.so`, etc.) and scans their memory maps for exported or internal symbols.

## Building and Running

### Prerequisites
- CMake 3.22.1+
- Android NDK (r21e+ recommended, API 27+ target)
- Ninja build system (optional but recommended)

### Build Commands
```bash
# Set NDK_HOME environment variable
export NDK_HOME=/path/to/android-ndk

# Initialize submodules
git submodule update --init --recursive

# Configure and Build
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Output: `build/libAndSwapChainHook.so`

### Running/Injection
The resulting `.so` file must be injected into the target process. [AndKittyInjector](https://github.com/MJx0/AndKittyInjector) is the recommended tool:
```bash
./AndKittyInjector --package <package_name> -lib libAndSwapChainHook.so --memfd --hide --watch
```

## Development Conventions

### Coding Style & Patterns
- **Modern C++:** Use C++20 features (concepts, ranges, `std::atomic`, `std::future`).
- **X-Macros:** Used in `source/Core/ElfScannerManager.h` for library registration. To add a new library for scanning, simply add an entry to the `ELF_LIB_LIST` macro.
- **Thread Safety:** Most core managers (`SwapChainHook`, `InputEventHook`) use `std::atomic` and `std::mutex` to handle asynchronous initialization and hook states.
- **Logging:** Use the centralized logging macros in `Utils/Logger.h` (e.g., `LOGI`, `LOGE`). Many modules define their own prefixed logging for easier debugging (e.g., `SCH_LOGI`).
- **Resource Management:** `ResourceManager` handles assets like fonts and styles, ensuring they are loaded into the appropriate graphics context.

### Hooking Guidelines
- Always verify symbol resolution before attempting a `DobbyHook`.
- In Vulkan hooks, ensure that the helper device/instance is cleaned up to avoid memory leaks.
- In EGL hooks, the state save/restore logic is critical for stability; avoid modifying the GL state outside the protected blocks.

### File Structure
- `external/`: Submodules and third-party headers (Dobby, ImGui, KittyMemory).
- `source/AndroidPlatform/`: JNI and Android-specific glue.
- `source/Core/`: Symbol management and resource loading.
- `source/InputEvent/`: Input hooking and handling.
- `source/SwapChain/`: Graphics API hooking logic.
- `source/Utils/`: Logging, file I/O, and crash handling.
