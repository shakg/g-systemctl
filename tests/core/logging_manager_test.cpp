#include <gtest/gtest.h>
#include <g-systemctl/core/logging_manager.hpp>
#include <cstdlib>

namespace gsystemctl {

TEST(LoggingManagerTest, CreateReturnsNonNull) {
    auto manager = LoggingManager::create(nullptr);
    EXPECT_NE(manager, nullptr);
}

TEST(LoggingManagerTest, OpenLogsWithoutTmuxFails) {
    // ensure TMUX is not set for the duration of the test
    unsetenv("TMUX");
    auto manager = LoggingManager::create(nullptr);
    auto [success, message] = manager->open_logs("foo.service");
    EXPECT_FALSE(success);
    EXPECT_NE(message.find("tmux"), std::string::npos);
}

} // namespace gsystemctl
