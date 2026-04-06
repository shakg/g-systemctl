#pragma once

#include <string>
#include <memory>
#include <utility>

namespace gsystemctl {

class CommandExecutor; // forward

class LoggingManager {
public:
    virtual ~LoggingManager() = default;

    /// Open a log viewer for the given unit.  When running inside tmux this will
    /// split the current pane and invoke "journalctl -u <unit> -f".  Returns a
    /// pair of (success,message) where the message contains human readable error
    /// information in case of failure.
    virtual std::pair<bool, std::string> open_logs(const std::string& unit) = 0;

    static std::unique_ptr<LoggingManager> create(
        std::shared_ptr<CommandExecutor> executor);
};

} // namespace gsystemctl
