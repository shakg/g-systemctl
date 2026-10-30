#include <gtest/gtest.h>
#include <g-systemctl/platform/linux_service_manager.hpp>
#include <g-systemctl/platform/macos_service_manager.hpp>
#include <cstdlib>
#include <string>
#include <vector>

namespace gsystemctl {

namespace {

struct RecordedCommand {
    std::string command;
    bool privileged;
    std::string password;
};

class RecordingCommandExecutor : public CommandExecutor {
public:
    std::vector<RecordedCommand> commands;
    CommandResult next_result{0, "", ""};
    int next_interactive_exit_code = 0;

    CommandResult execute(const std::string& command) override {
        commands.push_back({command, false, ""});
        return next_result;
    }

    CommandResult execute_privileged(const std::string& command) override {
        return execute_privileged(command, "");
    }

    CommandResult execute_privileged(const std::string& command, const std::string& password) override {
        commands.push_back({command, true, password});
        return next_result;
    }

    bool can_execute_privileged_without_password() override {
        return true;
    }

    int execute_interactive(const std::string& command, bool privileged) override {
        commands.push_back({command, privileged, ""});
        return next_interactive_exit_code;
    }
};

} // namespace

TEST(LinuxDaemonReloadTest, SystemModeRunsPrivilegedReloadWithPassword) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    LinuxServiceManager manager(executor, true);

    auto [success, message] = manager.daemon_reload("secret");

    EXPECT_TRUE(success);
    EXPECT_EQ(message, "");
    ASSERT_EQ(executor->commands.size(), 1u);
    EXPECT_EQ(executor->commands[0].command, "systemctl daemon-reload");
    EXPECT_TRUE(executor->commands[0].privileged);
    EXPECT_EQ(executor->commands[0].password, "secret");
}

TEST(LinuxDaemonReloadTest, UserModeReloadsUserManagerWithoutSudo) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    LinuxServiceManager manager(executor, false);

    EXPECT_TRUE(manager.daemon_reload().first);
    ASSERT_EQ(executor->commands.size(), 1u);
    EXPECT_EQ(executor->commands[0].command, "systemctl --user daemon-reload");
    EXPECT_FALSE(executor->commands[0].privileged);
}

TEST(LinuxDaemonReloadTest, ReportsIncorrectSudoPassword) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    executor->next_result = {1, "Sorry, try again.\nsudo: 1 incorrect password attempt\n", ""};
    LinuxServiceManager manager(executor, true);

    auto [success, message] = manager.daemon_reload("wrong");

    EXPECT_FALSE(success);
    EXPECT_EQ(message, "incorrect password");
}

TEST(LinuxDaemonReloadTest, ReportsSudoTimeout) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    executor->next_result = {1, "sudo: timed out reading password\n", ""};
    LinuxServiceManager manager(executor, true);

    EXPECT_EQ(manager.daemon_reload("pw").second, "sudo password prompt timed out");
}

TEST(LinuxDaemonReloadTest, ReportsCommandOutputOnOtherFailures) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    executor->next_result = {1, "Failed to reload daemon: Access denied\n", ""};
    LinuxServiceManager manager(executor, true);

    EXPECT_EQ(manager.daemon_reload().second, "Failed to reload daemon: Access denied");
}

TEST(LinuxEditServiceTest, SystemModeEditsFullUnitWithVimViaSudo) {
    unsetenv("GSYSTEMCTL_EDITOR");
    auto executor = std::make_shared<RecordingCommandExecutor>();
    LinuxServiceManager manager(executor, true);

    EXPECT_TRUE(manager.edit_service("ssh.service").first);
    ASSERT_EQ(executor->commands.size(), 1u);
    EXPECT_EQ(executor->commands[0].command, "env SYSTEMD_EDITOR='vim' systemctl edit --full 'ssh.service'");
    EXPECT_TRUE(executor->commands[0].privileged);
}

TEST(LinuxEditServiceTest, UserModeUsesUserManagerAndEditorOverride) {
    setenv("GSYSTEMCTL_EDITOR", "nvim", 1);
    auto executor = std::make_shared<RecordingCommandExecutor>();
    LinuxServiceManager manager(executor, false);

    manager.edit_service("it's.service");
    unsetenv("GSYSTEMCTL_EDITOR");

    ASSERT_EQ(executor->commands.size(), 1u);
    EXPECT_EQ(executor->commands[0].command, "env SYSTEMD_EDITOR='nvim' systemctl --user edit --full 'it'\\''s.service'");
    EXPECT_FALSE(executor->commands[0].privileged);
}

TEST(LinuxEditServiceTest, ReportsEditorFailure) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    executor->next_interactive_exit_code = 1;
    LinuxServiceManager manager(executor, true);

    auto [success, message] = manager.edit_service("ssh.service");

    EXPECT_FALSE(success);
    EXPECT_EQ(message, "systemctl edit exited with code 1");
}

TEST(MacOSUnitFileTest, DaemonReloadIsNoOp) {
    auto executor = std::make_shared<RecordingCommandExecutor>();
    MacOSServiceManager manager(executor);

    EXPECT_FALSE(manager.supports_unit_files());
    EXPECT_TRUE(manager.daemon_reload("pw").first);
    EXPECT_FALSE(manager.edit_service("com.example.agent").first);
    EXPECT_TRUE(executor->commands.empty());
}

} // namespace gsystemctl
