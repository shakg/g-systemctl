#include "g-systemctl/container/docker_container_manager.hpp"
#include "g-systemctl/core/process_stream.hpp"
#include "g-systemctl/core/shell.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace gsystemctl {

namespace {

// Field separator in our --format templates. Docker never emits tabs in the
// fields we request, so splitting on it is unambiguous.
constexpr char kSep = '\t';

const char* const kPsFormat =
    "{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.State}}\t{{.Status}}\t{{.Ports}}\t"
    "{{.Label \"com.docker.compose.project\"}}";

const char* const kStatsFormat =
    "{{.ID}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}\t{{.NetIO}}\t{{.BlockIO}}\t{{.PIDs}}";

// One "key<TAB>value..." record per line.
const char* const kInspectFormat =
    "id\t{{.Id}}\n"
    "name\t{{.Name}}\n"
    "image\t{{.Config.Image}}\n"
    "state\t{{.State.Status}}\n"
    "created\t{{.Created}}\n"
    "started\t{{.State.StartedAt}}\n"
    "restarts\t{{.RestartCount}}\n"
    "entrypoint\t{{json .Config.Entrypoint}}\n"
    "cmd\t{{json .Config.Cmd}}\n"
    "nano_cpus\t{{.HostConfig.NanoCpus}}\n"
    "memory\t{{.HostConfig.Memory}}\n"
    "memory_swap\t{{.HostConfig.MemorySwap}}\n"
    "pids_limit\t{{.HostConfig.PidsLimit}}\n"
    "restart_policy\t{{.HostConfig.RestartPolicy.Name}}\n"
    "restart_retries\t{{.HostConfig.RestartPolicy.MaximumRetryCount}}\n"
    "compose_project\t{{index .Config.Labels \"com.docker.compose.project\"}}\n"
    "compose_dir\t{{index .Config.Labels \"com.docker.compose.project.working_dir\"}}\n"
    "{{range .Config.Env}}env\t{{.}}\n{{end}}"
    "{{range .Mounts}}mount\t{{.Source}}\t{{.Destination}}\t{{.RW}}\n{{end}}"
    "{{range $p, $b := .NetworkSettings.Ports}}port\t{{$p}}\t{{range $b}}{{.HostIp}}:{{.HostPort}} {{end}}\n{{end}}"
    "{{range $k, $v := .NetworkSettings.Networks}}network\t{{$k}}\t{{$v.IPAddress}}\n{{end}}";

std::string trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

// Splits on kSep, keeping empty fields.
std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t pos = line.find(kSep, start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
}

// Go templates print these for nil values.
std::string clean(const std::string& value) {
    auto trimmed = trim(value);
    if (trimmed == "<no value>" || trimmed == "null" || trimmed == "<nil>") {
        return "";
    }
    return trimmed;
}

int64_t to_int(const std::string& value) {
    auto cleaned = clean(value);
    if (cleaned.empty()) {
        return 0;
    }
    char* end = nullptr;
    long long parsed = std::strtoll(cleaned.c_str(), &end, 10);
    return end == cleaned.c_str() ? 0 : static_cast<int64_t>(parsed);
}

std::string describe_failure(const CommandResult& result) {
    // The first line carries the error; the CLI follows it with usage text.
    std::string output = trim(result.stdout_output + result.stderr_output);
    auto newline = output.find('\n');
    if (newline != std::string::npos) {
        output = trim(output.substr(0, newline));
    }
    if (output.empty()) {
        return "exit code " + std::to_string(result.exit_code);
    }
    // Docker prefixes daemon errors; the rest is the useful part.
    const std::string prefix = "Error response from daemon: ";
    if (output.rfind(prefix, 0) == 0) {
        output.erase(0, prefix.size());
    }
    return output;
}

} // namespace

std::unique_ptr<ContainerManager> ContainerManager::create(std::shared_ptr<CommandExecutor> executor) {
    const char* binary = std::getenv("GSYSTEMCTL_DOCKER");
    return std::make_unique<DockerContainerManager>(std::move(executor), binary && *binary ? binary : "docker");
}

DockerContainerManager::DockerContainerManager(std::shared_ptr<CommandExecutor> executor, std::string binary)
    : executor_(std::move(executor)), binary_(std::move(binary)) {}

std::string DockerContainerManager::runtime_name() const {
    auto slash = binary_.find_last_of('/');
    return slash == std::string::npos ? binary_ : binary_.substr(slash + 1);
}

ContainerManager::Result DockerContainerManager::check_available() {
    auto result = executor_->execute(shell_quote(binary_) + " version --format " + shell_quote("{{.Server.Version}}"));
    if (result.exit_code == 127) {
        return {false, binary_ + " not found in PATH"};
    }
    if (result.exit_code != 0) {
        return {false, describe_failure(result)};
    }
    return {true, trim(result.stdout_output)};
}

std::vector<Container> DockerContainerManager::list_containers() {
    auto result = executor_->execute(shell_quote(binary_) + " ps -a --format " + shell_quote(kPsFormat));
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    return parse_ps_output(result.stdout_output);
}

std::map<std::string, ContainerStats> DockerContainerManager::container_stats() {
    auto result = executor_->execute(shell_quote(binary_) + " stats --no-stream --format " + shell_quote(kStatsFormat));
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    return parse_stats_output(result.stdout_output);
}

ContainerDetails DockerContainerManager::inspect(const std::string& id) {
    auto result = executor_->execute(shell_quote(binary_) + " inspect --type container --format " +
                                     shell_quote(kInspectFormat) + " " + shell_quote(id));
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    return parse_inspect_output(result.stdout_output);
}

ContainerManager::Result DockerContainerManager::run_simple(const std::string& args) {
    auto result = executor_->execute(shell_quote(binary_) + " " + args);
    if (result.exit_code != 0) {
        return {false, describe_failure(result)};
    }
    return {true, ""};
}

ContainerManager::Result DockerContainerManager::start(const std::string& id) {
    return run_simple("start " + shell_quote(id));
}

ContainerManager::Result DockerContainerManager::stop(const std::string& id) {
    return run_simple("stop " + shell_quote(id));
}

ContainerManager::Result DockerContainerManager::restart(const std::string& id) {
    return run_simple("restart " + shell_quote(id));
}

std::string DockerContainerManager::build_update_command(const std::string& id, const ResourceUpdate& update) const {
    std::string command = shell_quote(binary_) + " update";
    auto add = [&command](const char* flag, const std::optional<std::string>& value) {
        if (value) {
            command += std::string(" ") + flag + " " + shell_quote(*value);
        }
    };
    add("--cpus", update.cpus);
    add("--memory", update.memory);
    add("--memory-swap", update.memory_swap);
    add("--pids-limit", update.pids_limit);
    add("--restart", update.restart_policy);
    return command + " " + shell_quote(id);
}

ContainerManager::Result DockerContainerManager::update_resources(const std::string& id, const ResourceUpdate& update) {
    if (update.empty()) {
        return {true, "nothing changed"};
    }
    auto result = executor_->execute(build_update_command(id, update));
    if (result.exit_code != 0) {
        return {false, describe_failure(result)};
    }
    return {true, ""};
}

std::unique_ptr<LogStream> DockerContainerManager::stream_logs(const std::string& id, int tail,
                                                               LoggingManager::LogCallback on_line) {
    try {
        return std::make_unique<ProcessLogStream>(
            std::vector<std::string>{binary_, "logs", "-f", "--tail", std::to_string(tail), id}, on_line);
    } catch (const std::exception& e) {
        if (on_line) {
            on_line(std::string("Unable to start ") + binary_ + " logs: " + e.what());
        }
        return nullptr;
    }
}

std::vector<Container> DockerContainerManager::parse_ps_output(const std::string& output) {
    std::vector<Container> containers;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_fields(line);
        if (fields.size() < 6) {
            continue; // warnings or blank lines
        }
        Container c;
        c.id = trim(fields[0]);
        c.name = trim(fields[1]);
        c.image = trim(fields[2]);
        c.state = trim(fields[3]);
        c.status = trim(fields[4]);
        c.ports = trim(fields[5]);
        if (fields.size() > 6) {
            c.compose_project = clean(fields[6]);
        }
        if (!c.id.empty()) {
            containers.push_back(std::move(c));
        }
    }
    std::sort(containers.begin(), containers.end(), [](const Container& a, const Container& b) {
        if (a.is_running() != b.is_running()) {
            return a.is_running();
        }
        return a.name < b.name;
    });
    return containers;
}

std::map<std::string, ContainerStats> DockerContainerManager::parse_stats_output(const std::string& output) {
    std::map<std::string, ContainerStats> stats;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_fields(line);
        if (fields.size() < 7) {
            continue;
        }
        auto id = trim(fields[0]);
        if (id.empty()) {
            continue;
        }
        ContainerStats s;
        s.cpu = trim(fields[1]);
        s.mem_usage = trim(fields[2]);
        s.mem_percent = trim(fields[3]);
        s.net_io = trim(fields[4]);
        s.block_io = trim(fields[5]);
        s.pids = trim(fields[6]);
        stats[id] = std::move(s);
    }
    return stats;
}

ContainerDetails DockerContainerManager::parse_inspect_output(const std::string& output) {
    ContainerDetails d;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split_fields(line);
        if (fields.size() < 2) {
            continue;
        }
        const auto& key = fields[0];
        const auto& value = fields[1];

        if (key == "env") {
            // Values may legitimately contain tabs; rejoin the rest of the line.
            d.env.push_back(line.substr(4));
        } else if (key == "mount" && fields.size() >= 4) {
            d.mounts.push_back(trim(fields[1]) + " -> " + trim(fields[2]) + (trim(fields[3]) == "true" ? " (rw)" : " (ro)"));
        } else if (key == "port" && fields.size() >= 3) {
            auto bindings = trim(fields[2]);
            d.ports.push_back(trim(fields[1]) + (bindings.empty() ? " (not published)" : " -> " + bindings));
        } else if (key == "network" && fields.size() >= 3) {
            auto ip = trim(fields[2]);
            d.networks.push_back(trim(fields[1]) + (ip.empty() ? "" : " " + ip));
        } else if (key == "id") {
            d.id = clean(value);
        } else if (key == "name") {
            d.name = clean(value);
            if (!d.name.empty() && d.name.front() == '/') {
                d.name.erase(0, 1);
            }
        } else if (key == "image") {
            d.image = clean(value);
        } else if (key == "state") {
            d.state = clean(value);
        } else if (key == "created") {
            d.created = clean(value);
        } else if (key == "started") {
            d.started_at = clean(value);
        } else if (key == "restarts") {
            d.restart_count = clean(value);
        } else if (key == "entrypoint") {
            d.entrypoint = clean(value);
        } else if (key == "cmd") {
            d.command = clean(value);
        } else if (key == "nano_cpus") {
            d.nano_cpus = to_int(value);
        } else if (key == "memory") {
            d.memory = to_int(value);
        } else if (key == "memory_swap") {
            d.memory_swap = to_int(value);
        } else if (key == "pids_limit") {
            d.pids_limit = to_int(value);
        } else if (key == "restart_policy") {
            d.restart_policy = clean(value);
        } else if (key == "restart_retries") {
            d.restart_max_retries = static_cast<int>(to_int(value));
        } else if (key == "compose_project") {
            d.compose_project = clean(value);
        } else if (key == "compose_dir") {
            d.compose_dir = clean(value);
        }
    }
    return d;
}

} // namespace gsystemctl
