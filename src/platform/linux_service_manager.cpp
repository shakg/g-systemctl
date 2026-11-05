#include "g-systemctl/platform/linux_service_manager.hpp"
#include "g-systemctl/core/shell.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace gsystemctl {

namespace {

std::string trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::string describe_failure(const CommandResult& result) {
    std::string output = trim(result.stdout_output + result.stderr_output);
    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.find("incorrect password") != std::string::npos || lower.find("sorry, try again") != std::string::npos) {
        return "incorrect password";
    }
    if (lower.find("timed out") != std::string::npos) {
        return "sudo password prompt timed out";
    }
    if (output.empty()) {
        return "exit code " + std::to_string(result.exit_code);
    }
    return output;
}

// systemctl prints these for counters that are unset or not tracked.
bool is_unset(const std::string& value) {
    return value.empty() || value == "[not set]" || value == "infinity" || value == "18446744073709551615";
}

std::string format_size(const std::string& bytes_text) {
    double value = std::strtod(bytes_text.c_str(), nullptr);
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(unit == 0 ? 0 : 1);
    out << value << " " << units[unit];
    return out.str();
}

std::string format_cpu_time(const std::string& nsec_text) {
    double seconds = std::strtod(nsec_text.c_str(), nullptr) / 1e9;
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(3);
    if (seconds >= 60.0) {
        long minutes = static_cast<long>(seconds / 60.0);
        out.precision(1);
        out << minutes << "min " << (seconds - static_cast<double>(minutes) * 60.0) << "s";
    } else {
        out << seconds << "s";
    }
    return out.str();
}

// ExecStart looks like "{ path=/usr/bin/x ; argv[]=/usr/bin/x -a ; ignore_errors=no ; ... }".
std::string exec_command(const std::string& value) {
    const std::string marker = "argv[]=";
    auto begin = value.find(marker);
    if (begin == std::string::npos) {
        return value;
    }
    begin += marker.size();
    auto end = value.find(" ; ignore_errors=", begin);
    return trim(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
}

std::string editor_command() {
    const char* editor = std::getenv("GSYSTEMCTL_EDITOR");
    return editor && *editor ? editor : "vim";
}

} // namespace

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
    add_missing_unit_files(units);
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

std::vector<ServiceUnit> LinuxServiceManager::parse_systemctl_unit_files_output(const std::string& output) {
    std::vector<ServiceUnit> units;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        std::istringstream line_stream(line);
        std::string unit_name;
        std::string state;
        if (!(line_stream >> unit_name >> state)) {
            continue;
        }

        ServiceUnit unit;
        unit.unit = unit_name;
        unit.load = "loaded";
        unit.active = "inactive";
        unit.sub = "dead";
        unit.description = state;
        units.push_back(unit);
    }

    return units;
}

void LinuxServiceManager::add_missing_unit_files(std::vector<ServiceUnit>& units) {
    std::string command = system_mode_
        ? "systemctl list-unit-files -t service --full --plain --no-legend --no-pager"
        : "systemctl --user list-unit-files -t service --full --plain --no-legend --no-pager";

    auto result = executor_->execute(command);
    if (result.exit_code != 0) {
        return;
    }

    auto unit_files = parse_systemctl_unit_files_output(result.stdout_output);
    for (auto& unit_file : unit_files) {
        auto existing = std::find_if(units.begin(), units.end(), [&unit_file](const ServiceUnit& unit) {
            return unit.unit == unit_file.unit;
        });
        if (existing == units.end()) {
            units.push_back(std::move(unit_file));
        }
    }

    std::sort(units.begin(), units.end(), [](const ServiceUnit& left, const ServiceUnit& right) {
        return left.unit < right.unit;
    });
}

ServiceDetails LinuxServiceManager::describe_service(const std::string& name) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute(
        prefix + " show " + shell_quote(name) + " --no-pager -p " +
        "Description,LoadState,ActiveState,SubState,UnitFileState,FragmentPath,Type,MainPID,"
        "ActiveEnterTimestamp,InactiveEnterTimestamp,TasksCurrent,MemoryCurrent,CPUUsageNSec,"
        "Restart,NRestarts,User,ExecStart,WantedBy");
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    return parse_show_output(result.stdout_output);
}

ServiceDetails LinuxServiceManager::parse_show_output(const std::string& output) {
    std::map<std::string, std::string> props;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            props[line.substr(0, eq)] = trim(line.substr(eq + 1));
        }
    }
    auto get = [&props](const std::string& key) {
        auto it = props.find(key);
        return it == props.end() ? std::string() : it->second;
    };

    ServiceDetails rows;
    auto add = [&rows](const std::string& label, const std::string& value) {
        if (!value.empty()) {
            rows.emplace_back(label, value);
        }
    };

    add("Description", get("Description"));

    std::string state = get("ActiveState");
    if (!get("SubState").empty()) {
        state += " (" + get("SubState") + ")";
    }
    const bool active = get("ActiveState") == "active" || get("ActiveState") == "reloading";
    const std::string since = active ? get("ActiveEnterTimestamp") : get("InactiveEnterTimestamp");
    if (!is_unset(since)) {
        state += " since " + since;
    }
    add("State", state);

    std::string unit_file = get("UnitFileState");
    if (!get("FragmentPath").empty()) {
        unit_file += (unit_file.empty() ? "" : " · ") + get("FragmentPath");
    }
    add("Unit file", unit_file.empty() ? get("LoadState") : unit_file);
    add("Type", get("Type"));

    if (!is_unset(get("MainPID")) && get("MainPID") != "0") {
        add("Main PID", get("MainPID"));
    }
    if (!is_unset(get("TasksCurrent"))) {
        add("Tasks", get("TasksCurrent"));
    }
    if (!is_unset(get("MemoryCurrent"))) {
        add("Memory", format_size(get("MemoryCurrent")));
    }
    if (!is_unset(get("CPUUsageNSec"))) {
        add("CPU time", format_cpu_time(get("CPUUsageNSec")));
    }

    std::string restart = get("Restart");
    if (!is_unset(get("NRestarts")) && get("NRestarts") != "0") {
        restart += (restart.empty() ? "" : " · ") + get("NRestarts") + " restarts";
    }
    add("Restart", restart);
    add("User", get("User"));
    if (!get("ExecStart").empty()) {
        add("Command", exec_command(get("ExecStart")));
    }
    add("Wanted by", get("WantedBy"));
    return rows;
}

ServiceSample LinuxServiceManager::sample_service(const std::string& name) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute(prefix + " show " + shell_quote(name) +
                                     " --no-pager -p CPUUsageNSec,MemoryCurrent,TasksCurrent,IOReadBytes,IOWriteBytes");
    if (result.exit_code != 0) {
        return {};
    }
    return parse_sample_output(result.stdout_output);
}

ServiceSample LinuxServiceManager::parse_sample_output(const std::string& output) {
    ServiceSample sample;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, eq);
        auto value = trim(line.substr(eq + 1));
        if (is_unset(value)) {
            continue;
        }
        char* end = nullptr;
        double number = std::strtod(value.c_str(), &end);
        if (end == value.c_str()) {
            continue;
        }
        if (key == "CPUUsageNSec") {
            sample.cpu_nsec = number;
        } else if (key == "MemoryCurrent") {
            sample.memory_bytes = number;
        } else if (key == "TasksCurrent") {
            sample.tasks = number;
        } else if (key == "IOReadBytes") {
            sample.io_read_bytes = number;
        } else if (key == "IOWriteBytes") {
            sample.io_write_bytes = number;
        }
    }
    return sample;
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

std::pair<bool, std::string> LinuxServiceManager::start_service(const std::string& name, const std::string& password) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute_privileged(prefix + " start " + name, password);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::stop_service(const std::string& name, const std::string& password) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute_privileged(prefix + " stop " + name, password);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::restart_service(const std::string& name, const std::string& password) {
    std::string prefix = system_mode_ ? "systemctl" : "systemctl --user";
    auto result = executor_->execute_privileged(prefix + " restart " + name, password);
    return {result.exit_code == 0, result.stdout_output};
}

std::pair<bool, std::string> LinuxServiceManager::toggle_service(const ServiceUnit& service, const std::string& password) {
    if (service.is_running()) {
        return stop_service(service.unit, password);
    }
    return start_service(service.unit, password);
}

std::pair<bool, std::string> LinuxServiceManager::daemon_reload(const std::string& password) {
    auto result = system_mode_
        ? executor_->execute_privileged("systemctl daemon-reload", password)
        : executor_->execute("systemctl --user daemon-reload");
    if (result.exit_code == 0) {
        return {true, ""};
    }
    return {false, describe_failure(result)};
}

std::pair<bool, std::string> LinuxServiceManager::edit_service(const std::string& name) {
    // `systemctl edit` copies the unit to /etc (or ~/.config) on first edit and
    // runs daemon-reload after a successful save. `env` keeps SYSTEMD_EDITOR across sudo.
    std::string command = "env SYSTEMD_EDITOR=" + shell_quote(editor_command()) +
        (system_mode_ ? " systemctl" : " systemctl --user") + " edit --full " + shell_quote(name);
    int exit_code = executor_->execute_interactive(command, system_mode_);
    if (exit_code == 0) {
        return {true, ""};
    }
    return {false, "systemctl edit exited with code " + std::to_string(exit_code)};
}

} // namespace gsystemctl
