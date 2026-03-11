#include "g-systemctl/platform/macos_logging_manager.hpp"

namespace gsystemctl {

MacOSLoggingManager::MacOSLoggingManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::pair<bool, std::string> MacOSLoggingManager::open_logs(
    const std::string& /*unit*/) {
    return {false, "Logging manager is not supported on macOS"};
}

} // namespace gsystemctl
