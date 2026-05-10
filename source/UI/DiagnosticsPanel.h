#pragma once

#include <string>
#include <vector>

namespace DiagnosticsPanel {

void Render();

[[nodiscard]] std::string ResolveLogRoot();
[[nodiscard]] std::vector<std::string> ListCrashLogs();
[[nodiscard]] std::string ReadCrashLogTail(const std::string& path, std::size_t maxBytes = 16 * 1024);

} // namespace DiagnosticsPanel
