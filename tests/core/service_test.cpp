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