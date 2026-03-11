#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/platform/platform.hpp"
#include "g-systemctl/platform/linux_logging_manager.hpp"
#include "g-systemctl/platform/macos_logging_manager.hpp"
#include <stdexcept>

namespace gsystemctl {

std::unique_ptr<LoggingManager> LoggingManager::create(
    std::shared_ptr<CommandExecutor> executor) {
    Platform platform = detect_platform();

    switch (platform) {
        case Platform::Linux:
            return std::make_unique<LinuxLoggingManager>(executor);
        case Platform::MacOS:
            return std::make_unique<MacOSLoggingManager>(executor);
        default:
            throw std::runtime_error("Unsupported platform");
    }
}

} // namespace gsystemctl
