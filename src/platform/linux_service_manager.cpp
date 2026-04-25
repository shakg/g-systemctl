#include "g-systemctl/platform/linux_service_manager.hpp"
#include <map>
#include <sstream>
#include <stdexcept>

namespace gsystemctl {

LinuxServiceManager::LinuxServiceManager(std::shared_ptr<CommandExecutor> executor, bool system_mode)
    : executor_(std::move(executor)), system_mode_(system_mode) {}

std::vector<ServiceUnit> LinuxServiceManager::list_services() {
    std::string command = system_mode_
        ? "systemctl list-units -t service --full --all --plain --no-legend --no-pager"
        : "systemctl --user list-units -t service --full --all --plain --no-legend --no-pager";

    auto result = executor_->execute(command);

    if (result.exit_code != 0) {
        throw std::runtime_error("Failed to list services: " + result.stderr_output);
    }

    auto units = parse_systemctl_output(result.stdout_output);
    populate_resource_usage(units);
    return units;
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

void LinuxServiceManager::populate_resource_usage(std::vector<ServiceUnit>& units) {
    std::vector<std::string> running_units;
    for (const auto& unit : units) {
        if (unit.is_running()) {
            running_units.push_back(unit.unit);
        }
    }

    if (running_units.empty()) {
        return;
    }

    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    std::ostringstream show_command;
    show_command << prefix << " show --property=MainPID --value";
    for (const auto& unit_name : running_units) {
        show_command << " " << unit_name;
    }

    auto pid_result = executor_->execute(show_command.str());
    if (pid_result.exit_code != 0) {
        return;
    }

    std::map<std::string, std::string> unit_pid;
    std::istringstream pid_stream(pid_result.stdout_output);
    std::string pid;
    size_t index = 0;
    while (index < running_units.size() && std::getline(pid_stream, pid)) {
        if (!pid.empty() && pid != "0") {
            unit_pid[running_units[index]] = pid;
        }
        ++index;
    }

    if (unit_pid.empty()) {
        return;
    }

    std::ostringstream ps_command;
    ps_command << "ps -o pid=,pcpu=,rss= -p ";
    bool first = true;
    for (const auto& [_, pid_value] : unit_pid) {
        if (!first) {
            ps_command << ",";
        }
        ps_command << pid_value;
        first = false;
    }

    auto ps_result = executor_->execute(ps_command.str());
    if (ps_result.exit_code != 0) {
        for (auto& unit : units) {
            auto pid_it = unit_pid.find(unit.unit);
            if (pid_it != unit_pid.end()) {
                unit.pid = pid_it->second;
            }
        }
        return;
    }

    std::map<std::string, std::pair<std::string, std::string>> process_usage;
    std::istringstream ps_stream(ps_result.stdout_output);
    std::string line;
    while (std::getline(ps_stream, line)) {
        std::istringstream line_stream(line);
        std::string process_pid;
        std::string cpu;
        long rss_kb = 0;
        if (!(line_stream >> process_pid >> cpu >> rss_kb)) {
            continue;
        }

        std::ostringstream memory;
        memory.setf(std::ios::fixed);
        memory.precision(1);
        memory << (static_cast<double>(rss_kb) / 1024.0) << "MB";
        process_usage[process_pid] = {cpu + "%", memory.str()};
    }

    for (auto& unit : units) {
        auto pid_it = unit_pid.find(unit.unit);
        if (pid_it == unit_pid.end()) {
            continue;
        }

        unit.pid = pid_it->second;
        auto usage_it = process_usage.find(pid_it->second);
        if (usage_it != process_usage.end()) {
            unit.cpu = usage_it->second.first;
            unit.memory = usage_it->second.second;
        }
    }
}

std::pair<bool, std::string> LinuxServiceManager::start_service(const std::string& name) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute_privileged(prefix + " start " + name);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::stop_service(const std::string& name) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute_privileged(prefix + " stop " + name);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::toggle_service(const ServiceUnit& service) {
    if (service.is_running()) {
        return stop_service(service.unit);
    }
    return start_service(service.unit);
}

} // namespace gsystemctl
