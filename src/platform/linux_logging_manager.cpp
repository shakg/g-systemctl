#include "g-systemctl/platform/linux_logging_manager.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace gsystemctl {

namespace {

class LinuxJournalStream : public LogStream {
public:
    LinuxJournalStream(const std::string& unit, bool system_mode, LoggingManager::LogCallback on_line)
        : on_line_(std::move(on_line)) {
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
        }

        pid_ = fork();
        if (pid_ < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            throw std::runtime_error(std::string("fork failed: ") + std::strerror(errno));
        }

        if (pid_ == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            if (system_mode) {
                execlp("journalctl", "journalctl", "-u", unit.c_str(), "-f", "-n", "100", "--no-pager", nullptr);
            } else {
                execlp("journalctl", "journalctl", "--user-unit", unit.c_str(), "-f", "-n", "100", "--no-pager", nullptr);
            }
            _exit(127);
        }

        read_fd_ = pipefd[0];
        close(pipefd[1]);
        reader_ = std::thread([this] { read_loop(); });
    }

    ~LinuxJournalStream() override {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
        }
        if (reader_.joinable()) {
            reader_.join();
        }
        if (pid_ > 0) {
            int status = 0;
            waitpid(pid_, &status, 0);
        }
    }

private:
    void read_loop() {
        std::array<char, 512> buffer;
        std::string pending;

        while (true) {
            ssize_t bytes = read(read_fd_, buffer.data(), buffer.size());
            if (bytes <= 0) {
                break;
            }
            pending.append(buffer.data(), static_cast<size_t>(bytes));

            size_t newline = 0;
            while ((newline = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (on_line_) {
                    on_line_(std::move(line));
                }
            }
        }

        if (!pending.empty() && on_line_) {
            on_line_(std::move(pending));
        }
        close(read_fd_);
        read_fd_ = -1;
    }

    pid_t pid_ = -1;
    int read_fd_ = -1;
    std::thread reader_;
    LoggingManager::LogCallback on_line_;
};

} // namespace

LinuxLoggingManager::LinuxLoggingManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::unique_ptr<LogStream> LinuxLoggingManager::stream_logs(
    const std::string& unit, bool system_mode, LogCallback on_line) {
    try {
        return std::make_unique<LinuxJournalStream>(unit, system_mode, std::move(on_line));
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
