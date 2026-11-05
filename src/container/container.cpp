#include "g-systemctl/container/container.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace gsystemctl {

namespace {

constexpr int64_t kKiB = 1024;
constexpr int64_t kMiB = kKiB * 1024;
constexpr int64_t kGiB = kMiB * 1024;

std::string trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

} // namespace

double parse_docker_size(const std::string& text) {
    auto value = trim(text);
    char* end = nullptr;
    double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || number < 0) {
        return -1;
    }
    std::string unit = trim(std::string(end));
    std::transform(unit.begin(), unit.end(), unit.begin(), [](unsigned char c) { return std::tolower(c); });
    struct Unit { const char* name; double factor; };
    static const Unit units[] = {
        {"", 1}, {"b", 1},
        {"kb", 1e3}, {"mb", 1e6}, {"gb", 1e9}, {"tb", 1e12},
        {"kib", 1024.0}, {"mib", 1048576.0}, {"gib", 1073741824.0}, {"tib", 1099511627776.0},
    };
    for (const auto& u : units) {
        if (unit == u.name) {
            return number * u.factor;
        }
    }
    return -1;
}

double parse_docker_percent(const std::string& text) {
    auto value = trim(text);
    if (!value.empty() && value.back() == '%') {
        value.pop_back();
    }
    char* end = nullptr;
    double number = std::strtod(value.c_str(), &end);
    return end == value.c_str() || *end != '\0' ? -1 : number;
}

std::pair<double, double> parse_docker_size_pair(const std::string& text) {
    auto slash = text.find('/');
    if (slash == std::string::npos) {
        return {-1, -1};
    }
    return {parse_docker_size(text.substr(0, slash)), parse_docker_size(text.substr(slash + 1))};
}

std::string format_cpus(int64_t nano_cpus) {
    if (nano_cpus <= 0) {
        return "";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(nano_cpus) / 1e9);
    std::string value = buffer;
    while (!value.empty() && value.back() == '0') {
        value.pop_back();
    }
    if (!value.empty() && value.back() == '.') {
        value.pop_back();
    }
    return value;
}

std::string format_memory_flag(int64_t bytes) {
    if (bytes <= 0) {
        return "";
    }
    if (bytes % kGiB == 0) {
        return std::to_string(bytes / kGiB) + "g";
    }
    if (bytes % kMiB == 0) {
        return std::to_string(bytes / kMiB) + "m";
    }
    if (bytes % kKiB == 0) {
        return std::to_string(bytes / kKiB) + "k";
    }
    return std::to_string(bytes);
}

std::string format_bytes(int64_t bytes) {
    if (bytes < 0) {
        return "unlimited";
    }
    const char* unit = "B";
    double value = static_cast<double>(bytes);
    if (bytes >= kGiB) {
        unit = "GiB";
        value /= static_cast<double>(kGiB);
    } else if (bytes >= kMiB) {
        unit = "MiB";
        value /= static_cast<double>(kMiB);
    } else if (bytes >= kKiB) {
        unit = "KiB";
        value /= static_cast<double>(kKiB);
    }
    char buffer[32];
    if (value == static_cast<double>(static_cast<int64_t>(value))) {
        std::snprintf(buffer, sizeof(buffer), "%lld%s", static_cast<long long>(value), unit);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f%s", value, unit);
    }
    return buffer;
}

ResourceSettings ContainerDetails::resource_settings() const {
    ResourceSettings settings;
    settings.cpus = format_cpus(nano_cpus);
    settings.memory = format_memory_flag(memory);
    settings.memory_swap = memory_swap < 0 ? "-1" : format_memory_flag(memory_swap);
    settings.pids_limit = pids_limit > 0 ? std::to_string(pids_limit) : "";
    if (restart_policy.empty()) {
        settings.restart_policy = "no";
    } else if (restart_policy == "on-failure" && restart_max_retries > 0) {
        settings.restart_policy = "on-failure:" + std::to_string(restart_max_retries);
    } else {
        settings.restart_policy = restart_policy;
    }
    return settings;
}

ResourceUpdateResult build_resource_update(const ResourceSettings& original, const ResourceSettings& edited) {
    ResourceUpdateResult result;
    auto& update = result.update;

    auto cpus = trim(edited.cpus);
    if (cpus != original.cpus) {
        if (cpus.empty()) {
            result.error = "Docker cannot remove a CPU limit in place; recreate the container";
            return result;
        }
        update.cpus = cpus;
    }

    auto memory = trim(edited.memory);
    if (memory != original.memory) {
        if (memory.empty()) {
            result.error = "Docker cannot remove a memory limit in place; recreate the container";
            return result;
        }
        update.memory = memory;
    }

    auto swap = trim(edited.memory_swap);
    if (swap != original.memory_swap) {
        update.memory_swap = swap.empty() ? "-1" : swap;
    }

    auto pids = trim(edited.pids_limit);
    if (pids != original.pids_limit) {
        update.pids_limit = pids.empty() ? "-1" : pids;
    }

    auto restart = trim(edited.restart_policy);
    if (restart.empty()) {
        restart = "no";
    }
    if (restart != original.restart_policy) {
        update.restart_policy = restart;
    }

    return result;
}

} // namespace gsystemctl
