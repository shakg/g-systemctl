#include "g-systemctl/core/service_manager.hpp"
#include "g-systemctl/platform/platform.hpp"
#include "g-systemctl/platform/linux_service_manager.hpp"
#include "g-systemctl/platform/macos_service_manager.hpp"
#include <stdexcept>

namespace gsystemctl {

std::unique_ptr<ServiceManager> ServiceManager::create(
    std::shared_ptr<CommandExecutor> executor
) {
    Platform platform = detect_platform();

    switch (platform) {
        case Platform::Linux:
            return std::make_unique<LinuxServiceManager>(executor);
        case Platform::MacOS:
            return std::make_unique<MacOSServiceManager>(executor);
        default:
            throw std::runtime_error("Unsupported platform");
    }
}

} // namespace gsystemctl
