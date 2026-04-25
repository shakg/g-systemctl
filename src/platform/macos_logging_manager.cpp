#include "g-systemctl/platform/macos_logging_manager.hpp"

namespace gsystemctl {

MacOSLoggingManager::MacOSLoggingManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::unique_ptr<LogStream> MacOSLoggingManager::stream_logs(
    const std::string& /*unit*/, bool /*system_mode*/, LogCallback on_line) {
    if (on_line) {
        on_line("Log streaming is not supported on macOS");
    }
    return nullptr;
}

std::pair<bool, std::string> MacOSLoggingManager::open_logs(
    const std::string& /*unit*/) {
    return {false, "Logging manager is not supported on macOS"};
}

} // namespace gsystemctl
