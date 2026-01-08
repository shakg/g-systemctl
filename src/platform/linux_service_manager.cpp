#include "g-systemctl/platform/linux_service_manager.hpp"
#include <sstream>
#include <stdexcept>

namespace gsystemctl {

LinuxServiceManager::LinuxServiceManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::vector<ServiceUnit> LinuxServiceManager::list_services() {
    auto result = executor_->execute(
        "systemctl list-units -t service --full --all --plain --no-legend --no-pager"
    );

    if (result.exit_code != 0) {
        throw std::runtime_error("Failed to list services: " + result.stderr_output);
    }

    return parse_systemctl_output(result.stdout_output);
}

std::vector<ServiceUnit> LinuxServiceManager::parse_systemctl_output(const std::string& output) {
    std::vector<ServiceUnit> units;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        std::istringstream line_stream(line);
        std::vector<std::string> fields;
        std::string field;

        while (line_stream >> field) {
            fields.push_back(field);
        }

        if (fields.size() < 5) continue;

        ServiceUnit unit;
        unit.unit = fields[0];
        unit.load = fields[1];
        unit.active = fields[2];
        unit.sub = fields[3];

        std::ostringstream desc;
        for (size_t i = 4; i < fields.size(); ++i) {
            if (i > 4) desc << " ";
            desc << fields[i];
        }
        unit.description = desc.str();

        units.push_back(unit);
    }

    return units;
}

std::pair<bool, std::string> LinuxServiceManager::start_service(const std::string& name) {
    auto result = executor_->execute_privileged("systemctl start " + name);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::stop_service(const std::string& name) {
    auto result = executor_->execute_privileged("systemctl stop " + name);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::toggle_service(const ServiceUnit& service) {
    if (service.is_running()) {
        return stop_service(service.unit);
    }
    return start_service(service.unit);
}

} // namespace gsystemctl
