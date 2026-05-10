#include "private/Logging.hpp"
#include <BNM/UserSettings/GlobalSettings.hpp>
#include <mutex>
#include <spdlog/sinks/android_sink.h>

namespace BNM::Internal {
std::shared_ptr<spdlog::logger> bnmLogger;
void SetupLogging() {
  static std::once_flag flag;
  std::call_once(flag, []() {
    auto android_sink =
        std::make_shared<spdlog::sinks::android_sink_mt>(BNM_TAG);
    bnmLogger = std::make_shared<spdlog::logger>("BNM", android_sink);

#ifdef BNM_DEBUG
    bnmLogger->set_level(spdlog::level::debug);
#elif defined(BNM_INFO)
            bnmLogger->set_level(spdlog::level::info);
#elif defined(BNM_WARNING)
            bnmLogger->set_level(spdlog::level::warn);
#elif defined(BNM_ERROR)
            bnmLogger->set_level(spdlog::level::err);
#else
            bnmLogger->set_level(spdlog::level::off);
#endif

    spdlog::set_default_logger(bnmLogger);

    // Pattern: [Tag] [Level] [File:Line] Message
    bnmLogger->set_pattern("[%n] [%l] [%s:%#] %v");
  });
}

void SetLogLevel(int level) {
  if (!bnmLogger)
    SetupLogging();
  bnmLogger->set_level((spdlog::level::level_enum)level);
}
} // namespace BNM::Internal
