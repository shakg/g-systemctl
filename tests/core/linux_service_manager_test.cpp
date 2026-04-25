#include <gtest/gtest.h>
#include <g-systemctl/platform/linux_service_manager.hpp>
#include <algorithm>
#include <string>
#include <vector>

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
