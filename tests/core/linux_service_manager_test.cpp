#include <gtest/gtest.h>
#include <g-systemctl/platform/linux_service_manager.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>

namespace gsystemctl {

namespace {

class FakeCommandExecutor : public CommandExecutor {
public:
    std::vector<std::string> commands;

    CommandResult execute(const std::string& command) override {
        commands.push_back(command);

        if (command.find("list-units") != std::string::npos) {
            return {0, "ssh.service loaded active running OpenBSD Secure Shell server\n", ""};
        }

        if (command.find("list-unit-files") != std::string::npos) {
            return {0, "example.service linked enabled\nssh.service enabled enabled\n", ""};
        }

        if (command.find("systemctl show") != std::string::npos) {
            return {0, "123\n", ""};
        }

        if (command.find("ps -o") != std::string::npos) {
            return {0, "123 0.1 1024\n", ""};
        }

        return {1, "", "unexpected command"};
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

TEST(LinuxServiceManagerTest, IncludesRegisteredUnitFilesMissingFromLoadedUnits) {
    auto executor = std::make_shared<FakeCommandExecutor>();
    LinuxServiceManager manager(executor, true);

    auto services = manager.list_services();

    auto example = std::find_if(services.begin(), services.end(), [](const ServiceUnit& service) {
        return service.unit == "example.service";
    });

    ASSERT_NE(example, services.end());
    EXPECT_EQ(example->load, "loaded");
    EXPECT_EQ(example->active, "inactive");
    EXPECT_EQ(example->sub, "dead");
    EXPECT_EQ(example->description, "linked");
}

} // namespace gsystemctl

namespace gsystemctl {

namespace {

// Records every command and returns canned systemctl output.
class RecordingExecutor : public CommandExecutor {
public:
    std::vector<std::string> commands;
    std::string list_units_output;

    CommandResult execute(const std::string& command) override {
        commands.push_back(command);
        if (command.find("list-units") != std::string::npos) {
            return {0, list_units_output, ""};
        }
        if (command.find("list-unit-files") != std::string::npos) {
            return {0, "", ""};
        }
        if (command.find("systemctl show") != std::string::npos) {
            return {0, "123\n", ""};
        }
        if (command.find("ps -o") != std::string::npos) {
            return {0, "123 0.1 1024\n", ""};
        }
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

TEST(LinuxServiceManagerTest, StartStopRestartShellQuoteUnitName) {
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxServiceManager manager(executor, true);

    // systemd-escaped names contain backslashes: the shell would eat the
    // backslash of \x2d if the name were not quoted.
    const std::string escaped = "home-user\\x2dname.mount";
    manager.start_service(escaped, "pw");
    manager.stop_service(escaped, "pw");
    manager.restart_service(escaped, "pw");

    ASSERT_EQ(executor->commands.size(), 3u);
    EXPECT_EQ(executor->commands[0], "systemctl start 'home-user\\x2dname.mount'");
    EXPECT_EQ(executor->commands[1], "systemctl stop 'home-user\\x2dname.mount'");
    EXPECT_EQ(executor->commands[2], "systemctl restart 'home-user\\x2dname.mount'");
}

TEST(LinuxServiceManagerTest, StartStopRestartQuoteUnitNameInUserMode) {
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxServiceManager manager(executor, false);

    manager.start_service("my unit@x.service", "pw");
    manager.stop_service("my unit@x.service", "pw");
    manager.restart_service("my unit@x.service", "pw");

    ASSERT_EQ(executor->commands.size(), 3u);
    EXPECT_EQ(executor->commands[0], "systemctl --user start 'my unit@x.service'");
    EXPECT_EQ(executor->commands[1], "systemctl --user stop 'my unit@x.service'");
    EXPECT_EQ(executor->commands[2], "systemctl --user restart 'my unit@x.service'");
}

TEST(LinuxServiceManagerTest, StartStopRestartQuoteHostileUnitNames) {
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxServiceManager manager(executor, true);

    manager.start_service("a\"b.service", "pw");
    manager.start_service("a'b.service", "pw");
    manager.start_service("$(touch /tmp/pwned).service", "pw");
    manager.start_service("x;`touch /tmp/pwned`.service", "pw");

    ASSERT_EQ(executor->commands.size(), 4u);
    EXPECT_EQ(executor->commands[0], "systemctl start 'a\"b.service'");
    EXPECT_EQ(executor->commands[1], "systemctl start 'a'\\''b.service'");
    EXPECT_EQ(executor->commands[2], "systemctl start '$(touch /tmp/pwned).service'");
    EXPECT_EQ(executor->commands[3], "systemctl start 'x;`touch /tmp/pwned`.service'");
}

TEST(LinuxServiceManagerTest, MainPidQueryShellQuotesUnitName) {
    auto executor = std::make_shared<RecordingExecutor>();
    executor->list_units_output =
        "home-user\\x2dname.mount loaded active running Mount home\n"
        "systemd-cryptsetup@luks\\x2dABC.service loaded active running Cryptography\n";
    LinuxServiceManager manager(executor, true);

    manager.list_services();

    std::string expected = "systemctl show --property=MainPID --value "
                           "'home-user\\x2dname.mount' 'systemd-cryptsetup@luks\\x2dABC.service'";
    auto it = std::find(executor->commands.begin(), executor->commands.end(), expected);
    if (it == executor->commands.end()) {
        for (const auto& command : executor->commands) {
            ADD_FAILURE() << "  " << command;
        }
    }
    ASSERT_NE(it, executor->commands.end()) << "expected: " << expected;
}

TEST(LinuxServiceManagerTest, DescribeAndSampleShellQuoteUnitName) {
    auto executor = std::make_shared<RecordingExecutor>();
    LinuxServiceManager manager(executor, true);

    manager.describe_service("home-user\\x2dname.mount");
    manager.sample_service("home-user\\x2dname.mount");

    ASSERT_EQ(executor->commands.size(), 2u);
    EXPECT_NE(executor->commands[0].find("systemctl show 'home-user\\x2dname.mount'"), std::string::npos);
    EXPECT_NE(executor->commands[1].find("systemctl show 'home-user\\x2dname.mount'"), std::string::npos);
}

} // namespace gsystemctl