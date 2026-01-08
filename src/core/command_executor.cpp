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

bool SystemCommandExecutor::is_root() const {
    return getuid() == 0;
}

} // namespace gsystemctl
