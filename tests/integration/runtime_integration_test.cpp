// Runs ContainerManager against a real runtime. Skipped unless
// GSYSTEMCTL_INTEGRATION=1; CI runs it against podman (.github/workflows/podman.yml).
//
//   GSYSTEMCTL_RUNTIME            docker / podman / auto, as for the app
//   GSYSTEMCTL_COMPOSE            compose command, as for the app
//   GSYSTEMCTL_TEST_IMAGE         image with sh (default docker.io/library/alpine:3.20)
//   GSYSTEMCTL_EXPECT_DRIFT       1 / 0: whether the compose command must support drift checks
//
// Creates containers named gsc-it-* and a compose project gsc-it-shop, and removes them afterwards.

#include <gtest/gtest.h>
#include <g-systemctl/container/cli_container_manager.hpp>
#include <g-systemctl/core/shell.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace gsystemctl {

namespace {

using Clock = std::chrono::steady_clock;

std::string env(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

bool enabled() { return env("GSYSTEMCTL_INTEGRATION") == "1"; }

std::string image() { return env("GSYSTEMCTL_TEST_IMAGE", "docker.io/library/alpine:3.20"); }

// Exits on SIGTERM within a second, so stops do not wait for the timeout.
const char* const kLoop = "trap 'exit 0' TERM; i=0; while :; do echo tick $i; i=$((i+1)); sleep 1; done";

std::optional<Container> find(const std::vector<Container>& list, const std::string& name) {
    for (const auto& c : list) {
        if (c.name == name) {
            return c;
        }
    }
    return std::nullopt;
}

// Collects streamed lines; waits for one containing `text`.
struct Lines {
    std::mutex mutex;
    std::vector<std::string> lines;

    LoggingManager::LogCallback callback() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(line);
        };
    }
    bool wait_for(const std::string& text, std::chrono::seconds timeout) {
        auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (const auto& line : lines) {
                    if (line.find(text) != std::string::npos) {
                        return true;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }
    std::string dump() {
        std::lock_guard<std::mutex> lock(mutex);
        std::string out;
        for (const auto& line : lines) {
            out += "  " + line + "\n";
        }
        return out;
    }
};

// Destroying a log stream must not hang (podman compose does not forward
// SIGTERM to its provider; ProcessLogStream kills the process group).
template <typename T>
double seconds_to_destroy(std::unique_ptr<T>& stream) {
    auto start = Clock::now();
    stream.reset();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

} // namespace

class RuntimeIntegration : public ::testing::Test {
protected:
    static inline std::shared_ptr<SystemCommandExecutor> executor;
    static inline std::unique_ptr<ContainerManager> manager;
    static inline bool podman = false;
    static inline bool rootless = false;
    static inline std::string cli;

    static CommandResult run(const std::string& args) { return executor->execute(shell_quote(cli) + " " + args); }

    static void remove_all() {
        run("rm -f gsc-it-logger gsc-it-cycle gsc-it-created gsc-it-in-pod");
        if (podman) {
            run("pod rm -f gsc-it-pod");
        }
    }

    static void SetUpTestSuite() {
        if (!enabled()) {
            return;
        }
        executor = std::make_shared<SystemCommandExecutor>();
        manager = ContainerManager::create(executor);
        auto* concrete = dynamic_cast<CliContainerManager*>(manager.get());
        podman = concrete && concrete->runtime() == ContainerRuntime::Podman;
        cli = env("GSYSTEMCTL_DOCKER", manager->runtime_name());
        rootless = podman && run("info --format '{{.Host.Security.Rootless}}'").stdout_output.rfind("true", 0) == 0;

        remove_all();
        const std::string img = shell_quote(image());
        auto check = [](const CommandResult& r, const std::string& what) {
            if (r.exit_code != 0) {
                ADD_FAILURE() << what << " failed: " << r.stdout_output << r.stderr_output;
            }
        };
        check(run("run -d --name gsc-it-logger --memory 64m -p 18089:80 -e GSC_IT=1 " + img + " sh -c " +
                  shell_quote(kLoop)),
              "run gsc-it-logger");
        check(run("run -d --name gsc-it-cycle " + img + " sh -c " + shell_quote(kLoop)), "run gsc-it-cycle");
        check(run("create --name gsc-it-created " + img + " true"), "create gsc-it-created");
        if (podman) {
            check(run("pod create --name gsc-it-pod"), "pod create");
            check(run("run -d --pod gsc-it-pod --name gsc-it-in-pod " + img + " sh -c " + shell_quote(kLoop)),
                  "run gsc-it-in-pod");
        }
    }

    static void TearDownTestSuite() {
        if (enabled() && manager) {
            remove_all();
            manager.reset();
        }
    }

    void SetUp() override {
        if (!enabled()) {
            GTEST_SKIP() << "set GSYSTEMCTL_INTEGRATION=1 to run against a real container runtime";
        }
    }
};

TEST_F(RuntimeIntegration, SelectsRuntimeAndIsAvailable) {
    auto wanted = env("GSYSTEMCTL_RUNTIME");
    if (wanted == "podman") {
        EXPECT_TRUE(podman);
    } else if (wanted == "docker") {
        EXPECT_FALSE(podman);
    }
    auto [ok, version] = manager->check_available();
    ASSERT_TRUE(ok) << version;
    EXPECT_FALSE(version.empty());
    std::cout << "runtime " << manager->runtime_name() << " " << version << (podman ? " (podman dialect)" : "")
              << "\n";
}

TEST_F(RuntimeIntegration, ListsContainersWithoutPodInfra) {
    auto list = manager->list_containers();
    auto logger = find(list, "gsc-it-logger");
    ASSERT_TRUE(logger);
    EXPECT_EQ(logger->state, "running");
    EXPECT_EQ(logger->id.size(), 12u);
    EXPECT_NE(logger->ports.find("18089"), std::string::npos) << logger->ports;
    EXPECT_FALSE(logger->compose_project.size()) << logger->compose_project;
    auto created = find(list, "gsc-it-created");
    ASSERT_TRUE(created);
    EXPECT_FALSE(created->is_running());
    if (podman) {
        EXPECT_TRUE(find(list, "gsc-it-in-pod"));
        for (const auto& c : list) {
            EXPECT_EQ(c.image.find("pause"), std::string::npos) << "infra container listed: " << c.name;
        }
    }
    // Running containers sort first.
    auto first_stopped = std::find_if(list.begin(), list.end(), [](const Container& c) { return !c.is_running(); });
    EXPECT_TRUE(std::none_of(first_stopped, list.end(), [](const Container& c) { return c.is_running(); }));
}

TEST_F(RuntimeIntegration, StatsParse) {
    auto logger = find(manager->list_containers(), "gsc-it-logger");
    ASSERT_TRUE(logger);
    auto stats = manager->container_stats();
    ASSERT_TRUE(stats.count(logger->id)) << "no stats for " << logger->id;
    const auto& s = stats.at(logger->id);
    EXPECT_GE(parse_docker_percent(s.cpu), 0) << s.cpu;
    auto [used, limit] = parse_docker_size_pair(s.mem_usage);
    EXPECT_GT(used, 0) << s.mem_usage;
    EXPECT_GT(limit, 0) << s.mem_usage;
    EXPECT_GE(parse_docker_size(s.pids), 1) << s.pids;
    EXPECT_GE(parse_docker_size_pair(s.net_io).first, 0) << s.net_io;
}

TEST_F(RuntimeIntegration, Inspects) {
    auto d = manager->inspect("gsc-it-logger");
    EXPECT_EQ(d.name, "gsc-it-logger");
    EXPECT_NE(d.image.find("alpine"), std::string::npos) << d.image;
    EXPECT_EQ(d.state, "running");
    EXPECT_EQ(d.memory, 64 * 1024 * 1024);
    EXPECT_FALSE(d.started_at.empty());
    EXPECT_NE(d.command.find("trap"), std::string::npos) << d.command;
    EXPECT_TRUE(std::find(d.env.begin(), d.env.end(), "GSC_IT=1") != d.env.end());
    ASSERT_FALSE(d.ports.empty());
    EXPECT_NE(d.ports[0].find("80/tcp -> "), std::string::npos) << d.ports[0];
    EXPECT_NE(d.ports[0].find("18089"), std::string::npos) << d.ports[0];
    // Rootless podman 4 uses slirp4netns: no named network.
    if (!rootless) {
        EXPECT_FALSE(d.networks.empty());
    }
    EXPECT_THROW(manager->inspect("gsc-it-missing"), std::runtime_error);
}

TEST_F(RuntimeIntegration, StopsStartsRestarts) {
    auto state = [] {
        auto c = find(manager->list_containers(), "gsc-it-cycle");
        return c ? c->state : std::string("missing");
    };
    auto r = manager->stop("gsc-it-cycle");
    ASSERT_TRUE(r.first) << r.second;
    EXPECT_EQ(state(), "exited");
    r = manager->start("gsc-it-cycle");
    ASSERT_TRUE(r.first) << r.second;
    EXPECT_EQ(state(), "running");
    r = manager->restart("gsc-it-cycle");
    ASSERT_TRUE(r.first) << r.second;
    EXPECT_EQ(state(), "running");

    r = manager->start("gsc-it-missing");
    EXPECT_FALSE(r.first);
    EXPECT_NE(r.second.find("gsc-it-missing"), std::string::npos) << r.second;
    EXPECT_NE(r.second.rfind("Error", 0), 0u) << "prefix not stripped: " << r.second;
}

TEST_F(RuntimeIntegration, UpdatesResources) {
    ResourceUpdate update;
    update.memory = "128m";
    update.memory_swap = "256m";
    update.pids_limit = "100";
    auto [ok, message] = manager->update_resources("gsc-it-cycle", update);
    EXPECT_TRUE(ok) << message;
}

TEST_F(RuntimeIntegration, FollowsLogs) {
    Lines lines;
    std::atomic<bool> ended{false};
    auto stream = manager->stream_logs("gsc-it-logger", 5, "", lines.callback(), [&ended] { ended = true; });
    ASSERT_TRUE(stream);
    EXPECT_TRUE(lines.wait_for("tick", std::chrono::seconds(10))) << lines.dump();
    EXPECT_LT(seconds_to_destroy(stream), 5.0);
    EXPECT_TRUE(ended);
}

class ComposeIntegration : public RuntimeIntegration {
protected:
    static inline std::string dir;
    static inline ComposeProjectRef ref;

    static void write_compose(const std::string& worker_extra) {
        const std::string loop = "trap 'exit 0' TERM; while :; do echo web up; sleep 1; done";
        std::ofstream out(dir + "/compose.yaml");
        out << "services:\n"
               "  web:\n"
               "    image: " << image() << "\n"
               "    command: [\"sh\", \"-c\", \"" << loop << "\"]\n"
               "  worker:\n"
               "    image: " << image() << "\n"
               "    command: [\"sh\", \"-c\", \"trap 'exit 0' TERM; while :; do sleep 1; done" << worker_extra << "\"]\n"
               "    depends_on: [web]\n";
    }

    static std::vector<Container> project_containers() {
        std::vector<Container> out;
        for (auto& c : manager->list_containers()) {
            if (c.compose_project == ref.name) {
                out.push_back(std::move(c));
            }
        }
        return out;
    }

    static void SetUpTestSuite() {
        RuntimeIntegration::SetUpTestSuite();
        if (!enabled()) {
            return;
        }
        char tmpl[] = "/tmp/gsc-it-XXXXXX";
        dir = std::string(mkdtemp(tmpl)) + "/gsc-it-shop";
        executor->execute("mkdir -p " + shell_quote(dir));
        write_compose("");
        ref.working_dir = dir;
        ref.config_files = {dir + "/compose.yaml"};
    }

    static void TearDownTestSuite() {
        if (enabled() && manager && !ref.name.empty()) {
            manager->compose(ref, ComposeAction::Down);
        }
        if (!dir.empty()) {
            executor->execute("rm -rf " + shell_quote(dir.substr(0, dir.find_last_of('/'))));
        }
        RuntimeIntegration::TearDownTestSuite();
    }
};

TEST_F(ComposeIntegration, FullLifecycle) {
    auto [compose_ok, compose_version] = manager->compose_available();
    ASSERT_TRUE(compose_ok) << compose_version;
    std::cout << "compose: " << compose_version << "\n";
    EXPECT_EQ(compose_version.find(">>>>"), std::string::npos) << "banner leaked: " << compose_version;
    if (auto expect = env("GSYSTEMCTL_EXPECT_DRIFT"); !expect.empty()) {
        EXPECT_EQ(manager->compose_drift_supported(), expect == "1");
    }

    // Name from `compose config`, or the directory name for podman-compose.
    ref.name = manager->compose_project_name(ref);
    EXPECT_EQ(ref.name, "gsc-it-shop");

    auto r = manager->compose(ref, ComposeAction::Up);
    ASSERT_TRUE(r.first) << r.second;

    auto containers = project_containers();
    ASSERT_EQ(containers.size(), 2u);
    for (const auto& c : containers) {
        EXPECT_EQ(c.state, "running") << c.name;
        EXPECT_EQ(c.compose_working_dir, dir) << c.name;
        EXPECT_EQ(split_compose_files(c.compose_config_files), ref.config_files) << c.name;
        EXPECT_EQ(c.compose_number, 1) << c.name;
    }
    auto projects = group_compose_projects(manager->list_containers(), {});
    auto project = std::find_if(projects.begin(), projects.end(),
                                [](const ComposeProject& p) { return p.ref.name == ref.name; });
    ASSERT_NE(project, projects.end());
    EXPECT_EQ(project->services, (std::vector<std::string>{"web", "worker"}));
    EXPECT_EQ(project->running, 2);

    if (manager->compose_drift_supported()) {
        auto hashes = manager->compose_config_hashes(ref);
        EXPECT_EQ(hashes.size(), 2u);
        EXPECT_TRUE(compose_drifted_services(containers, ref.name, hashes).empty());
        write_compose("; true");
        hashes = manager->compose_config_hashes(ref);
        EXPECT_EQ(compose_drifted_services(containers, ref.name, hashes), (std::vector<std::string>{"worker"}));
        write_compose("");
    }

    {
        Lines lines;
        auto stream = manager->stream_compose_logs(ref, 5, "", lines.callback(), nullptr);
        ASSERT_TRUE(stream);
        if (!lines.wait_for("web up", std::chrono::seconds(20))) {
            ADD_FAILURE() << "no \"web up\" from compose logs -f. Lines:\n" << lines.dump();
            auto compose = env("GSYSTEMCTL_COMPOSE", cli + " compose");
            for (const auto& command :
                 {compose + " -p " + ref.name + " -f " + ref.config_files[0] + " logs --tail 3",
                  cli + " ps -a --format '{{.Names}} {{.State}}'",
                  cli + " inspect --format '{{.Name}} log={{.HostConfig.LogConfig.Type}}' " + ref.name + "_web_1 " +
                      ref.name + "-web-1",
                  cli + " logs --tail 3 " + ref.name + "_web_1",
                  "timeout 6 " + cli + " logs -f --tail 2 " + ref.name + "_web_1 </dev/null | head -3",
                  "timeout 6 " + cli + " logs -f --tail 2 " + ref.name + "_web_1 " + ref.name + "_worker_1 </dev/null | head -3",
                  "timeout 8 env PYTHONUNBUFFERED=1 " + compose + " --verbose -p " + ref.name + " -f " +
                      ref.config_files[0] + " logs -f --no-color --tail 2 </dev/null | head -12"}) {
                auto r = executor->execute(command);
                std::cout << "$ " << command << " (exit " << r.exit_code << ")\n" << r.stdout_output << r.stderr_output;
            }
        }
        EXPECT_LT(seconds_to_destroy(stream), 5.0) << "compose log stream did not stop";
        std::lock_guard<std::mutex> lock(lines.mutex);
        for (const auto& line : lines.lines) {
            EXPECT_EQ(line.find(">>>>"), std::string::npos) << "banner in logs: " << line;
            EXPECT_EQ(line.find("\x1b[0m"), std::string::npos) << "escape in logs: " << line;
        }
    }

    r = manager->compose(ref, ComposeAction::Stop);
    ASSERT_TRUE(r.first) << r.second;
    for (const auto& c : project_containers()) {
        EXPECT_FALSE(c.is_running()) << c.name << " " << c.state;
    }
    r = manager->compose(ref, ComposeAction::Start);
    ASSERT_TRUE(r.first) << r.second;
    r = manager->compose(ref, ComposeAction::Restart);
    ASSERT_TRUE(r.first) << r.second;
    for (const auto& c : project_containers()) {
        EXPECT_TRUE(c.is_running()) << c.name << " " << c.state;
    }

    r = manager->compose(ref, ComposeAction::Down);
    ASSERT_TRUE(r.first) << r.second;
    EXPECT_TRUE(project_containers().empty());
}

} // namespace gsystemctl
