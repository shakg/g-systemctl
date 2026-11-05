#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gsystemctl {

// Live resource usage sample, as printed by `docker stats`.
struct ContainerStats {
    std::string cpu;          // "0.12%"
    std::string mem_usage;    // "1.1MiB / 128MiB"
    std::string mem_percent;  // "0.88%"
    std::string net_io;       // "84B / 84B"
    std::string block_io;     // "0B / 0B"
    std::string pids;         // "2"
};

// One row of `docker ps -a`, plus the latest stats sample if it is running.
struct Container {
    std::string id;     // short ID
    std::string name;
    std::string image;
    std::string state;  // created, running, paused, restarting, removing, exited, dead
    std::string status; // "Up 3 minutes", "Exited (0) 2 hours ago"
    std::string ports;
    std::string compose_project;
    ContainerStats stats;

    bool is_running() const { return state == "running"; }
    // Whether the start/stop toggle should stop this container.
    bool is_active() const { return state == "running" || state == "paused" || state == "restarting"; }
};

// Editable resource settings, as strings in `docker update` flag syntax.
// An empty string means "no limit".
struct ResourceSettings {
    std::string cpus;           // "1.5"
    std::string memory;         // "512m"
    std::string memory_swap;    // "1g", "-1" = unlimited swap
    std::string pids_limit;     // "100"
    std::string restart_policy; // "no", "always", "unless-stopped", "on-failure:3"
};

// Flags to pass to `docker update`. Unset fields are left unchanged.
struct ResourceUpdate {
    std::optional<std::string> cpus;
    std::optional<std::string> memory;
    std::optional<std::string> memory_swap;
    std::optional<std::string> pids_limit;
    std::optional<std::string> restart_policy;

    bool empty() const {
        return !cpus && !memory && !memory_swap && !pids_limit && !restart_policy;
    }
};

// Output of `docker inspect` that the UI shows.
struct ContainerDetails {
    std::string id;
    std::string name;
    std::string image;
    std::string state;
    std::string created;
    std::string started_at;
    std::string restart_count;
    std::string entrypoint;
    std::string command;
    std::string compose_project;
    std::string compose_dir;

    int64_t nano_cpus = 0;   // 0 = unlimited
    int64_t memory = 0;      // bytes, 0 = unlimited
    int64_t memory_swap = 0; // bytes, 0 = default, -1 = unlimited
    int64_t pids_limit = 0;  // <= 0 = unlimited
    std::string restart_policy;
    int restart_max_retries = 0;

    std::vector<std::string> env;      // KEY=VALUE
    std::vector<std::string> mounts;   // "src -> dst (rw)"
    std::vector<std::string> ports;    // "80/tcp -> 0.0.0.0:8080"
    std::vector<std::string> networks; // "bridge 172.17.0.2"

    ResourceSettings resource_settings() const;
};

// 500000000 -> "0.5"; 0 -> "".
std::string format_cpus(int64_t nano_cpus);
// 134217728 -> "128m"; 0 -> "". Uses the largest exact unit (k/m/g).
std::string format_memory_flag(int64_t bytes);
// 134217728 -> "128MiB"; for display only.
std::string format_bytes(int64_t bytes);

// Numbers from `docker stats` for the graphs. -1 when unparsable.
// "1.1MiB", "84B", "1.2kB", "3GB" -> bytes (binary and decimal units).
double parse_docker_size(const std::string& text);
// "12.5%" -> 12.5.
double parse_docker_percent(const std::string& text);
// "84B / 1.2kB" -> {84, 1200}.
std::pair<double, double> parse_docker_size_pair(const std::string& text);

// Diffs edited settings against the originals and returns only the changed
// flags. Returns an error message when a change cannot be expressed with
// `docker update` (Docker cannot remove a CPU or memory limit in place).
struct ResourceUpdateResult {
    ResourceUpdate update;
    std::string error;
};
ResourceUpdateResult build_resource_update(const ResourceSettings& original, const ResourceSettings& edited);

} // namespace gsystemctl
