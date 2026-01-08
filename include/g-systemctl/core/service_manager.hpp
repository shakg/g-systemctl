#pragma once

#include "service.hpp"
#include "command_executor.hpp"
#include <vector>
#include <memory>
#include <utility>
#include <string>

namespace gsystemctl {

class ServiceManager {
public:
    virtual ~ServiceManager() = default;
    virtual std::vector<ServiceUnit> list_services() = 0;
    virtual std::pair<bool, std::string> start_service(const std::string& name) = 0;
    virtual std::pair<bool, std::string> stop_service(const std::string& name) = 0;
    virtual std::pair<bool, std::string> toggle_service(const ServiceUnit& service) = 0;

    static std::unique_ptr<ServiceManager> create(std::shared_ptr<CommandExecutor> executor, bool system_mode = false);
};

} // namespace gsystemctl
