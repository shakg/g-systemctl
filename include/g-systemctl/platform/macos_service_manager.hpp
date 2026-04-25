#pragma once

#include "g-systemctl/core/service_manager.hpp"

namespace gsystemctl {

class MacOSServiceManager : public ServiceManager {
public:
    explicit MacOSServiceManager(std::shared_ptr<CommandExecutor> executor);

    std::vector<ServiceUnit> list_services() override;
    std::pair<bool, std::string> start_service(const std::string& name, const std::string& password = "") override;
    std::pair<bool, std::string> stop_service(const std::string& name, const std::string& password = "") override;
    std::pair<bool, std::string> restart_service(const std::string& name, const std::string& password = "") override;
    std::pair<bool, std::string> toggle_service(const ServiceUnit& service, const std::string& password = "") override;

private:
    std::shared_ptr<CommandExecutor> executor_;
    std::vector<ServiceUnit> parse_launchctl_output(const std::string& output);
};

} // namespace gsystemctl
