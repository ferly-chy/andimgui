# AndSwapChainHook

[中文](#中文) | [English](#english)

---

<a id="中文"></a>

### 构建

**环境要求：**
- CMake 3.22.1+
- Android NDK（ARM64-v8a，API 27+）
- C++20

```bash
# 设置 NDK_HOME 环境变量（替换为你的 NDK 实际路径）
export NDK_HOME=/path/to/android-ndk
git submodule update --init --recursive
cmake -B build -G "Ninja"
cmake --build build
```

输出：`libAndSwapChainHook.so`

### 使用方法

使用 [AndKittyInjector v5.1.0](https://github.com/MJx0/AndKittyInjector) 注入到目标应用：

```bash
./AndKittyInjector --package <包名> -lib libAndSwapChainHook.so --memfd --hide --watch
```

---

<a id="english"></a>

### Build

**Requirements:**
- CMake 3.22.1+
- Android NDK (ARM64-v8a, API 27+)
- C++20

```bash
# Set NDK_HOME environment variable (replace with your actual NDK path)
export NDK_HOME=/path/to/android-ndk
git submodule update --init --recursive
cmake -B build -G "Ninja"
cmake --build build
```

Output: `libAndSwapChainHook.so`

### Usage

Inject into a target app using [AndKittyInjector v5.1.0](https://github.com/MJx0/AndKittyInjector):

```bash
./AndKittyInjector --package <package_name> -lib libAndSwapChainHook.so --memfd --hide --watch
```

## Credits

This project uses the following libraries and tools:

### Injection & Memory
- [AndKittyInjector](https://github.com/MJx0/AndKittyInjector) - Android library injector
- [Dobby](https://github.com/jmpews/Dobby) - Inline hook framework
- [KittyMemory](https://github.com/MJx0/KittyMemory) - Memory manipulation library for Android
- [KittyMemoryEx](https://github.com/MJx0/KittyMemoryEx) - Extended memory manipulation library (pvm syscall)

### UI & Graphics
- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI library
- [omath](https://github.com/orange-cpp/omath) - Simple math library for C++
