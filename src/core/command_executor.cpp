#include "g-systemctl/core/command_executor.hpp"
#include <array>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>

namespace gsystemctl {

CommandResult SystemCommandExecutor::execute(const std::string& command) {
    CommandResult result;
    result.exit_code = 0;

    std::array<char, 256> buffer;
    std::string output;

    std::string full_command = command + " 2>&1";
    FILE* pipe = popen(full_command.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        result.stderr_output = "Failed to execute command";
        return result;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        output += buffer.data();
    }

    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = -1;
    }

    result.stdout_output = output;
    return result;
}

CommandResult SystemCommandExecutor::execute_privileged(const std::string& command) {
    if (is_root()) {
        return execute(command);
    }
    return execute("sudo " + command);
}

CommandResult SystemCommandExecutor::execute_privileged(const std::string& command, const std::string& password) {
    if (is_root()) {
        return execute(command);
    }
    if (password.empty()) {
        return execute_privileged(command);
    }
    return execute_with_input("sudo -S -p '' " + command + " 2>&1", password + "\n");
}

bool SystemCommandExecutor::can_execute_privileged_without_password() {
    if (is_root()) {
        return true;
    }
    return execute("sudo -n true").exit_code == 0;
}

bool SystemCommandExecutor::is_root() const {
    return getuid() == 0;
}

CommandResult SystemCommandExecutor::execute_with_input(const std::string& command, const std::string& input) {
    CommandResult result;
    result.exit_code = -1;

    int stdin_pipe[2];
    int output_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(output_pipe) == -1) {
        result.stderr_output = "Failed to create command pipes";
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        result.stderr_output = "Failed to fork command process";
        return result;
    }

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(output_pipe[1]);

    write(stdin_pipe[1], input.data(), input.size());
    close(stdin_pipe[1]);

    std::array<char, 256> buffer;
    std::string output;
    ssize_t bytes_read = 0;
    while ((bytes_read = read(output_pipe[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<size_t>(bytes_read));
    }
    close(output_pipe[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        result.stderr_output = "Failed to wait for command process";
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    result.stdout_output = output;
    return result;
}

} // namespace gsystemctl
