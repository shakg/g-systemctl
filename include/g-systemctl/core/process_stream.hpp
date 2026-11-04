#pragma once

#include "g-systemctl/core/logging_manager.hpp"
#include <functional>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace gsystemctl {

// Runs a command (no shell) and delivers each line of its stdout/stderr to a
// callback from a background thread. Destroying the stream terminates the
// process and joins the reader thread.
class ProcessLogStream : public LogStream {
public:
    using ExitCallback = std::function<void()>;

    // Throws std::runtime_error if the process cannot be started. `on_exit`
    // runs on the reader thread once output ends, whether the process exited
    // by itself or was terminated by the destructor.
    ProcessLogStream(const std::vector<std::string>& argv, LoggingManager::LogCallback on_line,
                     ExitCallback on_exit = {});
    ~ProcessLogStream() override;

    ProcessLogStream(const ProcessLogStream&) = delete;
    ProcessLogStream& operator=(const ProcessLogStream&) = delete;

private:
    void read_loop();

    pid_t pid_ = -1;
    int read_fd_ = -1;
    std::thread reader_;
    LoggingManager::LogCallback on_line_;
    ExitCallback on_exit_;
};

} // namespace gsystemctl
