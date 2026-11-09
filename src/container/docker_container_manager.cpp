#include "g-systemctl/container/docker_container_manager.hpp"
#include "g-systemctl/core/process_stream.hpp"
#include "g-systemctl/core/shell.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

namespace gsystemctl {

namespace {

// Field separator in our --format templates. Docker never emits tabs in the
// fields we request, so splitting on it is unambiguous.
constexpr char kSep = '\t';

const char* const kPsFormat =
    "{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.State}}\t{{.Status}}\t{{.Ports}}\t"
    "{{.Label \"com.docker.compose.project\"}}\t"
    "{{.Label \"com.docker.compose.service\"}}\t"
    "{{.Label \"com.docker.compose.container-number\"}}\t"
    "{{.Label \"com.docker.compose.project.working_dir\"}}\t"
    "{{.Label \"com.docker.compose.project.config_files\"}}\t"
    "{{.Label \"com.docker.compose.config-hash\"}}\t"
    "{{.Label \"com.docker.compose.oneoff\"}}\t"
    "{{.Label \"com.docker.compose.depends_on\"}}";

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

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

bool file_exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string join_quoted(const std::vector<std::string>& argv) {
    std::string out;
    for (const auto& arg : argv) {
        out += (out.empty() ? "" : " ") + shell_quote(arg);
    }
    return out;
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

std::vector<std::string> DockerContainerManager::build_logs_argv(const std::string& id, int tail,
                                                                 const std::string& since) const {
    std::vector<std::string> argv = {binary_, "logs", "-f", "--tail", tail < 0 ? "all" : std::to_string(tail)};
    if (!since.empty()) {
        argv.insert(argv.end(), {"--since", since});
    }
    argv.push_back(id);
    return argv;
}

std::unique_ptr<LogStream> DockerContainerManager::stream_logs(const std::string& id, int tail, const std::string& since,
                                                               LoggingManager::LogCallback on_line,
                                                               std::function<void()> on_end) {
    try {
        return std::make_unique<ProcessLogStream>(build_logs_argv(id, tail, since), on_line, std::move(on_end));
    } catch (const std::exception& e) {
        if (on_line) {
            on_line(std::string("Unable to start ") + binary_ + " logs: " + e.what());
        }
        return nullptr;
    }
}

// ---- compose ----------------------------------------------------------------

const std::vector<std::string>& DockerContainerManager::compose_prefix() {
    std::call_once(compose_once_, [this] {
        std::vector<std::vector<std::string>> candidates;
        if (const char* env = std::getenv("GSYSTEMCTL_COMPOSE"); env && *env) {
            std::vector<std::string> words;
            std::istringstream stream(env);
            for (std::string word; stream >> word;) {
                words.push_back(word);
            }
            candidates.push_back(words);
        } else {
            candidates.push_back({binary_, "compose"});
            candidates.push_back({"docker-compose"});
        }
        for (const auto& prefix : candidates) {
            auto args = prefix;
            args.insert(args.end(), {"version", "--short"});
            auto result = executor_->execute(join_quoted(args));
            if (result.exit_code == 0) {
                compose_prefix_ = prefix;
                compose_version_ = trim(result.stdout_output);
                return;
            }
            if (compose_error_.empty()) {
                compose_error_ = result.exit_code == 127 ? prefix[0] + " not found in PATH" : describe_failure(result);
            }
        }
        compose_error_ = "docker compose is not installed (" + compose_error_ + ")";
    });
    return compose_prefix_;
}

ContainerManager::Result DockerContainerManager::compose_available() {
    const auto& prefix = compose_prefix();
    if (prefix.empty()) {
        return {false, compose_error_};
    }
    std::string name;
    for (const auto& word : prefix) {
        auto slash = word.find_last_of('/');
        name += (name.empty() ? "" : " ") + (slash == std::string::npos ? word : word.substr(slash + 1));
    }
    return {true, name + " " + compose_version_};
}

std::string DockerContainerManager::build_compose_command(const std::vector<std::string>& compose,
                                                          const ComposeProjectRef& project,
                                                          const std::vector<std::string>& args, bool with_files) {
    auto argv = compose;
    if (!project.name.empty()) {
        argv.insert(argv.end(), {"-p", project.name});
    }
    if (with_files) {
        for (const auto& file : project.config_files) {
            argv.insert(argv.end(), {"-f", file});
        }
    }
    argv.insert(argv.end(), args.begin(), args.end());
    // From the working directory, relative paths (env_file, build context) resolve as when it was created.
    if (with_files && !project.working_dir.empty()) {
        return "cd " + shell_quote(project.working_dir) + " && " + join_quoted(argv);
    }
    return join_quoted(argv);
}

ContainerManager::Result DockerContainerManager::compose(const ComposeProjectRef& project, ComposeAction action) {
    const auto& prefix = compose_prefix();
    if (prefix.empty()) {
        return {false, compose_error_};
    }
    std::string missing;
    for (const auto& file : project.config_files) {
        if (!file_exists(file)) {
            missing = file;
            break;
        }
    }
    const bool dir_ok = project.working_dir.empty() || file_exists(project.working_dir);
    const bool needs_files = action == ComposeAction::Up || action == ComposeAction::UpBuild ||
                             action == ComposeAction::Pull;
    bool with_files = missing.empty() && dir_ok && (!project.config_files.empty() || !project.working_dir.empty());
    if (needs_files && !with_files) {
        return {false, !missing.empty() ? "compose file not found: " + missing
                       : !dir_ok        ? "directory not found: " + project.working_dir
                                        : "no compose file known for " + project.name};
    }
    auto result = executor_->execute(build_compose_command(prefix, project, compose_action_args(action), with_files));
    if (result.exit_code != 0) {
        // Compose prints progress on stderr; the error is the last line.
        auto output = trim(result.stderr_output.empty() ? result.stdout_output : result.stderr_output);
        auto newline = output.find_last_of('\n');
        if (newline != std::string::npos) {
            output = trim(output.substr(newline + 1));
        }
        return {false, output.empty() ? "exit code " + std::to_string(result.exit_code) : output};
    }
    return {true, ""};
}

std::string DockerContainerManager::parse_config_project_name(const std::string& output) {
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.rfind("name:", 0) == 0) {
            auto name = trim(line.substr(5));
            if (name.size() >= 2 && (name.front() == '"' || name.front() == '\'') && name.back() == name.front()) {
                name = name.substr(1, name.size() - 2);
            }
            return name;
        }
    }
    return "";
}

std::string DockerContainerManager::compose_project_name(const ComposeProjectRef& project) {
    const auto& prefix = compose_prefix();
    if (prefix.empty()) {
        throw std::runtime_error(compose_error_);
    }
    ComposeProjectRef files = project;
    files.name.clear();
    auto result = executor_->execute(build_compose_command(prefix, files, {"config"}, true));
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    auto name = parse_config_project_name(result.stdout_output);
    if (name.empty()) {
        throw std::runtime_error("compose config printed no project name");
    }
    return name;
}

std::map<std::string, std::string> DockerContainerManager::compose_config_hashes(const ComposeProjectRef& project) {
    const auto& prefix = compose_prefix();
    if (prefix.empty()) {
        throw std::runtime_error(compose_error_);
    }
    if (project.config_files.empty() && project.working_dir.empty()) {
        throw std::runtime_error("no compose file known for " + project.name);
    }
    if (!project.working_dir.empty() && !file_exists(project.working_dir)) {
        throw std::runtime_error("directory not found: " + project.working_dir);
    }
    auto result = executor_->execute(build_compose_command(prefix, project, {"config", "--hash", "*"}, true));
    if (result.exit_code != 0) {
        throw std::runtime_error(describe_failure(result));
    }
    return parse_compose_hashes(result.stdout_output);
}

std::vector<std::string> DockerContainerManager::build_compose_logs_argv(const std::vector<std::string>& compose,
                                                                         const ComposeProjectRef& project, int tail,
                                                                         const std::string& since) {
    // `-p` alone is enough for logs and keeps working if the files moved.
    auto argv = compose;
    argv.insert(argv.end(), {"-p", project.name, "logs", "-f", "--no-color", "--tail",
                             tail < 0 ? "all" : std::to_string(tail)});
    if (!since.empty()) {
        argv.insert(argv.end(), {"--since", since});
    }
    return argv;
}

std::unique_ptr<LogStream> DockerContainerManager::stream_compose_logs(const ComposeProjectRef& project, int tail,
                                                                       const std::string& since,
                                                                       LoggingManager::LogCallback on_line,
                                                                       std::function<void()> on_end) {
    const auto& prefix = compose_prefix();
    if (prefix.empty()) {
        if (on_line) {
            on_line(compose_error_);
        }
        return nullptr;
    }
    try {
        return std::make_unique<ProcessLogStream>(build_compose_logs_argv(prefix, project, tail, since), on_line,
                                                  std::move(on_end));
    } catch (const std::exception& e) {
        if (on_line) {
            on_line(std::string("Unable to start compose logs: ") + e.what());
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
        auto field = [&fields](size_t i) { return i < fields.size() ? clean(fields[i]) : std::string(); };
        c.compose_project = field(6);
        c.compose_service = field(7);
        c.compose_number = static_cast<int>(to_int(field(8)));
        c.compose_working_dir = field(9);
        c.compose_config_files = field(10);
        c.compose_config_hash = field(11);
        c.compose_oneoff = lower(field(12)) == "true";
        c.compose_depends_on = parse_compose_depends_on(field(13));
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
