#include "g-systemctl/core/process_stream.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace gsystemctl {

ProcessLogStream::ProcessLogStream(const std::vector<std::string>& argv, LoggingManager::LogCallback on_line,
                                   ExitCallback on_exit)
    : on_line_(std::move(on_line)), on_exit_(std::move(on_exit)) {
    if (argv.empty()) {
        throw std::runtime_error("empty command");
    }

    // Build argv before forking so the child does no allocation.
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& arg : argv) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

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
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(args[0], args.data());
        const char msg[] = "exec failed\n";
        (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(127);
    }

    read_fd_ = pipefd[0];
    close(pipefd[1]);
    reader_ = std::thread([this] { read_loop(); });
}

ProcessLogStream::~ProcessLogStream() {
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

void ProcessLogStream::read_loop() {
    std::array<char, 4096> buffer;
    std::string pending;

    while (true) {
        ssize_t bytes = read(read_fd_, buffer.data(), buffer.size());
        if (bytes < 0 && errno == EINTR) {
            continue;
        }
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
    if (on_exit_) {
        on_exit_();
    }
}

} // namespace gsystemctl
