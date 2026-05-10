#include "ResourceManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "Utils/Logger.h"

namespace {

using FontCandidate = std::pair<std::uintmax_t, std::string>;

constexpr std::array<std::string_view, 3> kFontDirs = {
    "/system/fonts",
    "/system/font",
    "/data/fonts",
};

constexpr std::array<std::string_view, 5> kPreferredChineseFonts = {
    "MiSansVF.ttf",
    "SourceSansPro-Bold.ttf",
    "DroidSansMono.ttf",
    "SysSans-Hans-Regular.ttf",
    "ZUKChinese.ttf",
};

[[nodiscard]] std::string ToLower(std::string value) {
    std::ranges::transform(value, value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] bool LooksLikeChineseFont(const std::filesystem::path& path) {
    const std::string extension = ToLower(path.extension().string());
    if (extension != ".ttf" && extension != ".otf") {
        return false;
    }

    const std::string filename = ToLower(path.filename().string());
    return filename.find("cjk") != std::string::npos ||
           filename.find("hans") != std::string::npos ||
           filename.find("chinese") != std::string::npos ||
           filename.find("sc") != std::string::npos ||
           filename.find("noto") != std::string::npos;
}

[[nodiscard]] std::optional<std::string> FindPreferredFontInDir(const std::filesystem::path& dir) {
    std::error_code ec;
    for (const auto fontName : kPreferredChineseFonts) {
        const std::filesystem::path fontPath = dir / fontName;
        if (std::filesystem::exists(fontPath, ec) && !ec) {
            return fontPath.string();
        }
        ec.clear();
    }
    return std::nullopt;
}

void CollectFallbackFontsInDir(const std::filesystem::path& dir, std::vector<FontCandidate>& candidateFonts) {
    std::error_code ec;
    std::filesystem::directory_iterator it{dir, ec};
    if (ec) {
        FLOGW("Error scanning directory {}: {}", dir.string(), ec.message());
        return;
    }

    const std::filesystem::directory_iterator end{};
    while (it != end) {
        const auto& entry = *it;

        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
        } else if (LooksLikeChineseFont(entry.path())) {
            const std::uintmax_t fileSize = std::filesystem::file_size(entry.path(), ec);
            if (ec) {
                ec.clear();
            } else {
                candidateFonts.emplace_back(fileSize, entry.path().string());
            }
        }

        it.increment(ec);
        if (ec) {
            FLOGW("Error scanning directory {}: {}", dir.string(), ec.message());
            ec.clear();
            break;
        }
    }
}

[[nodiscard]] std::optional<std::string> SelectLargestFallbackFont(std::vector<FontCandidate>& candidateFonts) {
    if (candidateFonts.empty()) {
        return std::nullopt;
    }

    std::ranges::sort(candidateFonts, [](const auto& lhs, const auto& rhs) {
        return lhs.first > rhs.first;
    });
    return candidateFonts.front().second;
}

} // namespace

ResourceManager& ResourceManager::GetInstance() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::reset() {
    m_CurrentFont = nullptr;
}

std::string ResourceManager::findSystemChineseFont() {
    // 遍历字体目录
    for (const auto dirName : kFontDirs) {
        const std::filesystem::path dir{dirName};
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) {
            continue;
        }

        // 按优先级查找字体
        if (auto preferredFont = FindPreferredFontInDir(dir)) {
            FLOGI("Found Chinese font: {}", *preferredFont);
            return *preferredFont;
        }

        // 如果优先列表中没找到，收集所有符合条件的字体并按大小排序
        std::vector<FontCandidate> candidateFonts;
        CollectFallbackFontsInDir(dir, candidateFonts);

        if (auto selectedFont = SelectLargestFallbackFont(candidateFonts)) {
            const auto& selected = candidateFonts.front();
            FLOGI("Found Chinese font (fallback, size: {} bytes): {}",
                 selected.first, selected.second);
            return *selectedFont;
        }
    }

    FLOGW("No Chinese font found in system directories");
    return "";
}

bool ResourceManager::initializeFonts(float fontSize) {
    m_CurrentFont = nullptr;
    ImGuiIO& io = ImGui::GetIO();

    // 尝试从系统目录加载中文字体
    std::string systemFontPath = findSystemChineseFont();

    if (!systemFontPath.empty()) {
        // 使用系统字体
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = true;
        // 字体粗细调整: <1.0更细, 1.0正常, >1.0更粗 (建议范围 0.8-1.5)
        fontConfig.RasterizerMultiply = 1.1f;

        m_CurrentFont = io.Fonts->AddFontFromFileTTF(
            systemFontPath.c_str(),
            fontSize,
            &fontConfig,
            io.Fonts->GetGlyphRangesChineseFull());

        if (m_CurrentFont) {
            FLOGI("Successfully loaded system Chinese font: {}", systemFontPath);
        } else {
            FLOGE("Failed to load system font from: {}", systemFontPath);
        }
    }

    // 如果系统字体加载失败，使用 ImGui 默认字体作为最终兜底
    if (!m_CurrentFont) {
        FLOGW("Falling back to ImGui default font");
        m_CurrentFont = io.Fonts->AddFontDefault();
    }

    if (!m_CurrentFont) {
        FLOGE("Failed to initialize any ImGui font");
        return false;
    }

    return true;
}
