#include "g-systemctl/platform/linux_logging_manager.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/process_stream.hpp"
#include <cstdlib>
#include <string>
#include <vector>

namespace gsystemctl {

LinuxLoggingManager::LinuxLoggingManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::unique_ptr<LogStream> LinuxLoggingManager::stream_logs(
    const std::string& unit, bool system_mode, LogCallback on_line) {
    try {
        std::vector<std::string> argv = {"journalctl"};
        if (system_mode) {
            argv.insert(argv.end(), {"-u", unit});
        } else {
            argv.insert(argv.end(), {"--user-unit", unit});
        }
        argv.insert(argv.end(), {"-f", "-n", "100", "--no-pager"});
        return std::make_unique<ProcessLogStream>(argv, on_line);
    } catch (const std::exception& e) {
        if (on_line) {
            on_line(std::string("Unable to start journalctl: ") + e.what());
        }
        return nullptr;
    }
}

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
