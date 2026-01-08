#include <gtest/gtest.h>
#include <g-systemctl/core/service_manager.hpp>
#include <g-systemctl/core/service.hpp>
#include <memory>
#include <utility>
#include <vector>

namespace gsystemctl {

TEST(ServiceManagerTest, CreateReturnsNonNull) {
    auto manager = ServiceManager::create(nullptr);
    EXPECT_NE(manager, nullptr);
}

TEST(ServiceUnitTest, ServiceManagerMethodSignatures) {
    ServiceUnit service;
    service.unit = "test.service";
    service.load = "loaded";
    service.active = "active";
    service.sub = "running";
    service.description = "Test service";
    
    EXPECT_TRUE(service.is_running());
    EXPECT_TRUE(service.is_loaded());
    EXPECT_EQ(service.unit, "test.service");
}

} // namespace gsystemctl