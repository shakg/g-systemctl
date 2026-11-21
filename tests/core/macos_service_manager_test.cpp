#include <gtest/gtest.h>
#include <g-systemctl/core/command_executor.hpp>
#include <g-systemctl/platform/macos_service_manager.hpp>
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

TEST(MacOSServiceManagerTest, StartStopShellQuoteLabel) {
    auto executor = std::make_shared<RecordingExecutor>();
    MacOSServiceManager manager(executor);

    manager.start_service("com.example.app", "pw");
    manager.stop_service("com.example.app", "pw");

    ASSERT_EQ(executor->commands.size(), 2u);
    EXPECT_EQ(executor->commands[0], "launchctl load 'com.example.app'");
    EXPECT_EQ(executor->commands[1], "launchctl unload 'com.example.app'");
}

TEST(MacOSServiceManagerTest, StartStopShellQuoteHostileLabel) {
    auto executor = std::make_shared<RecordingExecutor>();
    MacOSServiceManager manager(executor);

    manager.start_service("com.example.a'b", "pw");
    manager.stop_service("com.example.$(touch /tmp/pwned)", "pw");
    manager.restart_service("com.example.a b", "pw");

    ASSERT_EQ(executor->commands.size(), 4u);
    EXPECT_EQ(executor->commands[0], "launchctl load 'com.example.a'\\''b'");
    EXPECT_EQ(executor->commands[1], "launchctl unload 'com.example.$(touch /tmp/pwned)'");
    // restart = unload then load, both quoted.
    EXPECT_EQ(executor->commands[2], "launchctl unload 'com.example.a b'");
    EXPECT_EQ(executor->commands[3], "launchctl load 'com.example.a b'");
}

TEST(MacOSServiceManagerTest, DescribeShellQuotesLabel) {
    auto executor = std::make_shared<RecordingExecutor>();
    MacOSServiceManager manager(executor);

    manager.describe_service("com.example.a'b");

    ASSERT_EQ(executor->commands.size(), 1u);
    EXPECT_EQ(executor->commands[0], "launchctl list 'com.example.a'\\''b'");
}

} // namespace gsystemctl
