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
    virtual CommandResult execute_privileged(const std::string& command, const std::string& password) = 0;
    virtual bool can_execute_privileged_without_password() = 0;
};

class SystemCommandExecutor : public CommandExecutor {
public:
    CommandResult execute(const std::string& command) override;
    CommandResult execute_privileged(const std::string& command) override;
    CommandResult execute_privileged(const std::string& command, const std::string& password) override;
    bool can_execute_privileged_without_password() override;

private:
    bool is_root() const;
    CommandResult execute_with_input(const std::string& command, const std::string& input);
};

} // namespace gsystemctl
