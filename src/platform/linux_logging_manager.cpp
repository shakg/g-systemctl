#include "g-systemctl/platform/linux_logging_manager.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include <cstdlib>

namespace gsystemctl {

LinuxLoggingManager::LinuxLoggingManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::pair<bool, std::string> LinuxLoggingManager::open_logs(
    const std::string& unit) {
    // Ensure we're inside tmux before trying to split the pane.
    const char* tmux_env = std::getenv("TMUX");
    if (!tmux_env) {
        return {false, "Not running inside a tmux session"};
    }

    // Build the journalctl command. Quote the unit name to avoid shell issues.
    std::string command = "tmux split-window -v 'journalctl -u " + unit + " -f'";
    if (!executor_) {
        // If we don't have an executor, just run the command directly via system().
        int rc = std::system(command.c_str());
        if (rc != 0) {
            return {false, "failed to execute tmux command"};
        }
        return {true, ""};
    }

    auto result = executor_->execute(command);
    if (result.exit_code != 0) {
        std::string msg = "tmux command failed";
        if (!result.stderr_output.empty()) {
            msg = result.stderr_output;
        }
        return {false, msg};
    }
    return {true, ""};
}

} // namespace gsystemctl
