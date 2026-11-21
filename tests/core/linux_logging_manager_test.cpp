#include <gtest/gtest.h>
#include <g-systemctl/core/command_executor.hpp>
#include <g-systemctl/platform/linux_logging_manager.hpp>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace gsystemctl {

namespace {

class RecordingExecutor : public CommandExecutor {
public:
    std::vector<std::string> commands;

    CommandResult execute(const std::string& command) override {
        commands.push_back(command);
        return {0, "", ""};
    }

    CommandResult execute_privileged(const std::string& command) override {
        return execute(command);
    }

    CommandResult execute_privileged(const std::string& command, const std::string& password) override {
        (void)password;
        return execute(command);
    }

    bool can_execute_privileged_without_password() override {
        return true;
    }

    int execute_interactive(const std::string& command, bool privileged) override {
        (void)privileged;
        commands.push_back(command);
        return 0;
    }
};

} // namespace

TEST(LinuxLoggingManagerTest, OpenLogsShellQuotesUnitName) {
    setenv("TMUX", "fake", 1);
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxLoggingManager manager(executor);

    const std::string unit = "home-user\\x2dname.mount";
    auto [success, message] = manager.open_logs(unit);

    ASSERT_TRUE(success) << message;
    ASSERT_EQ(executor->commands.size(), 1u);
    // The unit name must be shell-quoted at both levels: inside the tmux
    // command line and in the outer shell that runs tmux.
    EXPECT_EQ(executor->commands[0],
              "tmux split-window -v 'journalctl -u '\\''home-user\\x2dname.mount'\\'' -f'");
    unsetenv("TMUX");
}

TEST(LinuxLoggingManagerTest, OpenLogsShellQuotesHostileUnitName) {
    setenv("TMUX", "fake", 1);
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxLoggingManager manager(executor);

    auto [success, message] = manager.open_logs("a';touch /tmp/pwned;'b.service");

    ASSERT_TRUE(success) << message;
    ASSERT_EQ(executor->commands.size(), 1u);
    const std::string& command = executor->commands[0];
    // No unescaped single quote may appear inside the unit-name region: the
    // only quotes are the shell_quote delimiters.
    EXPECT_EQ(command,
              "tmux split-window -v 'journalctl -u '\\''a'\\''\\'\\'''\\'';touch /tmp/pwned;'\\''\\'\\'''\\''b.service'\\'' -f'");
    unsetenv("TMUX");
}

} // namespace gsystemctl
