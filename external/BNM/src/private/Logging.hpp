#pragma once
#include <memory>
#include <spdlog/fmt/bundled/printf.h>
#include <spdlog/spdlog.h>

namespace BNM::Internal {
void SetupLogging();
void SetLogLevel(int level);
extern std::shared_ptr<spdlog::logger> bnmLogger;
} // namespace BNM::Internal
