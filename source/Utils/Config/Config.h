#pragma once

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

// ===== Config: 运行时配置数据 =====
struct Config {
    // --- 配置系统 ---
    bool bAutoLoadConfigOnStartup = true;

    // --- 界面 ---
    int  InjectionMode  = 0;        // 0 = SwapHook, 1 = Overlay
    int  RenderBackend  = 1;        // 0 = OpenGL, 1 = Vulkan
    bool bShowImGuiDraw = true;
    float FontScale     = 1.0f;
};

extern Config CFG;

// ===== 字段描述符: 用于自动序列化/反序列化 =====
struct ConfigField {
    std::string name;
    std::variant<bool*, int*, float*> ptr;
};

std::vector<ConfigField> GetConfigFields(Config& c);

// ===== 路径工具 =====
std::filesystem::path GetConfigSavePath();

// ===== 配置文件管理 =====
std::vector<std::string> ScanConfigFiles();
const std::string& GetActiveConfigName();
void SetActiveConfigName(const std::string& name);
std::filesystem::path GetActiveConfigFullPath();
std::string CreateNewConfigFile();

// ===== 配置读写 =====
void SaveConfig(const std::filesystem::path& path);
void LoadConfig(const std::filesystem::path& path);
void SaveActiveConfig();
void LoadActiveConfig();

// ===== 启动时自动加载 =====
void SaveLastActiveConfigName();
std::string LoadLastActiveConfigName();
void AutoLoadConfigOnStartup();
