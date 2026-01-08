#pragma once

#include <string>
#include <memory>

namespace gsystemctl {

struct CommandResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

class CommandExecutor {
public:
    virtual ~CommandExecutor() = default;
    virtual CommandResult execute(const std::string& command) = 0;
    virtual CommandResult execute_privileged(const std::string& command) = 0;
};

class SystemCommandExecutor : public CommandExecutor {
public:
    CommandResult execute(const std::string& command) override;
    CommandResult execute_privileged(const std::string& command) override;

private:
    bool is_root() const;
};

} // namespace gsystemctl
