#pragma once

#include <string>
#include <memory>
#include <utility>
#include <functional>

namespace gsystemctl {

class CommandExecutor; // forward

class LogStream {
public:
    virtual ~LogStream() = default;
};

class LoggingManager {
public:
    using LogCallback = std::function<void(std::string)>;

    virtual ~LoggingManager() = default;

    /// Start streaming logs for the given unit. The callback is invoked from a
    /// background thread.
    virtual std::unique_ptr<LogStream> stream_logs(
        const std::string& unit, bool system_mode, LogCallback on_line) = 0;

    /// Legacy tmux log viewer entry point.
    virtual std::pair<bool, std::string> open_logs(const std::string& unit) = 0;

    static std::unique_ptr<LoggingManager> create(
        std::shared_ptr<CommandExecutor> executor);
};

} // namespace gsystemctl
