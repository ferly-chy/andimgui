#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Config {

// /sdcard/Android/data/<pkg>/cache
std::filesystem::path GetSavePath();

// 仅文件名 (不带目录), 形如 ".conf_0"
std::vector<std::string> ScanFiles();

const std::string& GetActiveName();
void               SetActiveName(const std::string& name);
std::filesystem::path GetActiveFullPath();

// 自动取下一个未占用编号
std::string CreateNewFile();

void SaveToActive();
void LoadFromActive();

// 启动时自动加载上次活动配置 (用 .conf_active 记忆文件名, TU-private)
void AutoLoadOnStartup();

}  // namespace Config
