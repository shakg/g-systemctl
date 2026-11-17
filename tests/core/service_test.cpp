#include <gtest/gtest.h>
#include <g-systemctl/core/service.hpp>

namespace gsystemctl {

TEST(ServiceUnitTest, IsRunningReturnsTrueWhenSubIsRunning) {
    ServiceUnit service;
    service.sub = "running";
    EXPECT_TRUE(service.is_running());
}

TEST(ServiceUnitTest, IsRunningReturnsFalseWhenSubIsNotRunning) {
    ServiceUnit service;
    service.sub = "dead";
    EXPECT_FALSE(service.is_running());
}

TEST(ServiceUnitTest, IsLoadedReturnsTrueWhenLoadIsLoaded) {
    ServiceUnit service;
    service.load = "loaded";
    EXPECT_TRUE(service.is_loaded());
}

TEST(ServiceUnitTest, IsLoadedReturnsFalseWhenLoadIsNotLoaded) {
    ServiceUnit service;
    service.load = "not-found";
    EXPECT_FALSE(service.is_loaded());
}

TEST(ServiceUnitTest, DefaultConstruction) {
    ServiceUnit service;
    EXPECT_EQ(service.unit, "");
    EXPECT_EQ(service.load, "");
    EXPECT_EQ(service.active, "");
    EXPECT_EQ(service.sub, "");
    EXPECT_EQ(service.description, "");
}

} // namespace gsystemctl
namespace gsystemctl {

namespace {
ServiceState state_of(const std::string& active, const std::string& sub = "") {
    ServiceUnit unit;
    unit.active = active;
    unit.sub = sub;
    return service_state(unit);
}
} // namespace

TEST(ServiceStateTest, ActiveCanStopNotStart) {
    auto s = state_of("active", "running");
    EXPECT_EQ(s.kind, ServiceState::Kind::Active);
    EXPECT_EQ(s.label, "● active");
    EXPECT_FALSE(s.can_start);
    EXPECT_TRUE(s.can_stop);
}

TEST(ServiceStateTest, ActiveExitedIsActive) {
    EXPECT_EQ(state_of("active", "exited").kind, ServiceState::Kind::Active);
}

TEST(ServiceStateTest, FailedCanStartNotStop) {
    auto s = state_of("failed", "failed");
    EXPECT_EQ(s.kind, ServiceState::Kind::Failed);
    EXPECT_EQ(s.label, "✕ failed");
    EXPECT_TRUE(s.can_start);
    EXPECT_FALSE(s.can_stop);
}

TEST(ServiceStateTest, TransitionsShowTheirOwnLabel) {
    for (const std::string state : {"activating", "deactivating", "reloading", "refreshing", "maintenance"}) {
        auto s = state_of(state, "start");
        EXPECT_EQ(s.kind, ServiceState::Kind::Changing) << state;
        EXPECT_EQ(s.label, "◐ " + state);
        EXPECT_FALSE(s.can_start) << state;
        EXPECT_TRUE(s.can_stop) << state;
    }
}

TEST(ServiceStateTest, InactiveCanStartNotStop) {
    auto s = state_of("inactive", "dead");
    EXPECT_EQ(s.kind, ServiceState::Kind::Inactive);
    EXPECT_EQ(s.label, "○ inactive");
    EXPECT_TRUE(s.can_start);
    EXPECT_FALSE(s.can_stop);
}

TEST(ServiceStateTest, RunningSubStateCountsAsActive) {
    EXPECT_EQ(state_of("", "running").kind, ServiceState::Kind::Active);
}

TEST(ServiceStateTest, MacOSRunningAndStopped) {
    EXPECT_EQ(state_of("running", "running").kind, ServiceState::Kind::Active);
    EXPECT_EQ(state_of("stopped", "stopped").kind, ServiceState::Kind::Inactive);
}

} // namespace gsystemctl
