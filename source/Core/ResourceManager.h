#pragma once

#include <unordered_map>
#include <string>

#include "imgui/imgui.h"

class ResourceManager {
public:
    static ResourceManager& GetInstance();
    
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // 初始化字体资源
    bool initializeFonts(float fontSize);

    // 重置当前 ImGui context 关联的字体指针
    void reset();

    // 获取字体
    ImFont* getCurrentFont() const { return m_CurrentFont; }
    
private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    
    // 从系统目录查找中文字体
    std::string findSystemChineseFont();
    
    // 字体资源
    ImFont* m_CurrentFont = nullptr;
};
