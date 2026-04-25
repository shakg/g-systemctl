#pragma once

#include "g-systemctl/core/service_manager.hpp"

namespace gsystemctl {

class LinuxServiceManager : public ServiceManager {
public:
    explicit LinuxServiceManager(std::shared_ptr<CommandExecutor> executor, bool system_mode = false);

    std::vector<ServiceUnit> list_services() override;
    std::pair<bool, std::string> start_service(const std::string& name) override;
    std::pair<bool, std::string> stop_service(const std::string& name) override;
    std::pair<bool, std::string> toggle_service(const ServiceUnit& service) override;

private:
    std::shared_ptr<CommandExecutor> executor_;
    bool system_mode_;
    std::vector<ServiceUnit> parse_systemctl_output(const std::string& output);
    void populate_resource_usage(std::vector<ServiceUnit>& units);
};

} // namespace gsystemctl
