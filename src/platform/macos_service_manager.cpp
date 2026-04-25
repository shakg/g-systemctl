#include "g-systemctl/platform/macos_service_manager.hpp"
#include <sstream>
#include <stdexcept>

namespace gsystemctl {

MacOSServiceManager::MacOSServiceManager(std::shared_ptr<CommandExecutor> executor)
    : executor_(std::move(executor)) {}

std::vector<ServiceUnit> MacOSServiceManager::list_services() {
    auto result = executor_->execute("launchctl list");

    if (result.exit_code != 0) {
        throw std::runtime_error("Failed to list services: " + result.stderr_output);
    }

    return parse_launchctl_output(result.stdout_output);
}

std::vector<ServiceUnit> MacOSServiceManager::parse_launchctl_output(const std::string& output) {
    std::vector<ServiceUnit> units;
    std::istringstream stream(output);
    std::string line;
    bool first_line = true;

    while (std::getline(stream, line)) {
        if (first_line) {
            first_line = false;
            continue;
        }

        if (line.empty()) continue;

        std::istringstream line_stream(line);
        std::vector<std::string> fields;
        std::string field;

        while (line_stream >> field) {
            fields.push_back(field);
        }

        if (fields.size() < 3) continue;

        ServiceUnit unit;
        unit.unit = fields[2];
        unit.sub = (fields[0] != "-") ? "running" : "stopped";
        unit.active = unit.sub;
        unit.load = "loaded";
        unit.description = unit.unit;
        unit.pid = fields[0] != "-" ? fields[0] : "";

        units.push_back(unit);
    }

    return units;
}

std::pair<bool, std::string> MacOSServiceManager::start_service(const std::string& name, const std::string& password) {
    auto result = executor_->execute_privileged("launchctl load " + name, password);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> MacOSServiceManager::stop_service(const std::string& name, const std::string& password) {
    auto result = executor_->execute_privileged("launchctl unload " + name, password);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> MacOSServiceManager::restart_service(const std::string& name, const std::string& password) {
    auto stop_result = stop_service(name, password);
    if (!stop_result.first) {
        return stop_result;
    }
    return start_service(name, password);
}

std::pair<bool, std::string> MacOSServiceManager::toggle_service(const ServiceUnit& service, const std::string& password) {
    if (service.is_running()) {
        return stop_service(service.unit, password);
    }
    return start_service(service.unit, password);
}

} // namespace gsystemctl
