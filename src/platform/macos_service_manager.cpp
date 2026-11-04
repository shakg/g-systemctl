#include "g-systemctl/platform/macos_service_manager.hpp"
#include "g-systemctl/core/shell.hpp"
#include <cctype>
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

ServiceDetails MacOSServiceManager::describe_service(const std::string& name) {
    auto result = executor_->execute("launchctl list " + shell_quote(name));
    if (result.exit_code != 0) {
        throw std::runtime_error("launchctl list " + name + " failed: " + result.stderr_output);
    }
    return parse_launchctl_details(result.stdout_output);
}

ServiceDetails MacOSServiceManager::parse_launchctl_details(const std::string& output) {
    // Lines look like `\t"PID" = 123;`. Nested arrays/dicts are joined into one value.
    auto strip = [](std::string value) {
        while (!value.empty() && (value.back() == ';' || std::isspace(static_cast<unsigned char>(value.back())))) {
            value.pop_back();
        }
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
            ++start;
        }
        value = value.substr(start);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    };

    ServiceDetails rows;
    std::istringstream stream(output);
    std::string line;
    std::string nested_key;
    std::string nested_value;
    int depth = 0;
    while (std::getline(stream, line)) {
        auto trimmed = strip(line);
        if (depth == 0 && trimmed == "{") {
            depth = 1;
            continue;
        }
        if (depth == 1) {
            auto eq = line.find(" = ");
            if (eq == std::string::npos) {
                continue;
            }
            auto key = strip(line.substr(0, eq));
            auto value = strip(line.substr(eq + 3));
            if (value == "(" || value == "{") {
                nested_key = key;
                nested_value.clear();
                depth = 2;
                continue;
            }
            rows.emplace_back(key, value);
        } else if (depth == 2) {
            if (trimmed == ")" || trimmed == "}") {
                rows.emplace_back(nested_key, nested_value);
                depth = 1;
            } else if (!trimmed.empty()) {
                nested_value += (nested_value.empty() ? "" : " ") + trimmed;
            }
        }
    }
    return rows;
}

std::pair<bool, std::string> MacOSServiceManager::toggle_service(const ServiceUnit& service, const std::string& password) {
    if (service.is_running()) {
        return stop_service(service.unit, password);
    }
    return start_service(service.unit, password);
}

std::pair<bool, std::string> MacOSServiceManager::daemon_reload(const std::string& password) {
    (void)password;
    return {true, ""};
}

std::pair<bool, std::string> MacOSServiceManager::edit_service(const std::string& name) {
    (void)name;
    return {false, "editing service files is not supported on macOS"};
}

} // namespace gsystemctl
