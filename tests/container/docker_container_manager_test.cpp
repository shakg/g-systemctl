#include <gtest/gtest.h>
#include <g-systemctl/container/docker_container_manager.hpp>
#include <g-systemctl/core/process_stream.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace gsystemctl {

namespace {

class FakeExecutor : public CommandExecutor {
public:
    std::vector<std::string> commands;
    CommandResult next{0, "", ""};

    CommandResult execute(const std::string& command) override {
        commands.push_back(command);
        return next;
    }
    CommandResult execute_privileged(const std::string& command) override { return execute(command); }
    CommandResult execute_privileged(const std::string& command, const std::string&) override { return execute(command); }
    bool can_execute_privileged_without_password() override { return true; }
    int execute_interactive(const std::string&, bool) override { return 0; }
};

} // namespace

TEST(DockerContainerManagerTest, ParsesPsOutputAndSortsRunningFirst) {
    const std::string output =
        "98b52fc72ef5\tstopped-demo\talpine:3.20\texited\tExited (0) 2 hours ago\t\t\n"
        "63d352ff004b\tweb\tnginx:1.27\trunning\tUp 3 minutes\t0.0.0.0:8080->80/tcp\tshop\n"
        "WARNING: some daemon warning\n"
        "4bf5e90ed31e\tapi\tapi:latest\trunning\tUp 1 hour\t\t<no value>\n";

    auto containers = DockerContainerManager::parse_ps_output(output);
    ASSERT_EQ(containers.size(), 3u);
    EXPECT_EQ(containers[0].name, "api");
    EXPECT_EQ(containers[0].compose_project, "");
    EXPECT_EQ(containers[1].name, "web");
    EXPECT_EQ(containers[1].ports, "0.0.0.0:8080->80/tcp");
    EXPECT_EQ(containers[1].compose_project, "shop");
    EXPECT_TRUE(containers[1].is_running());
    EXPECT_EQ(containers[2].name, "stopped-demo");
    EXPECT_FALSE(containers[2].is_active());
}

TEST(DockerContainerManagerTest, ParsesStatsOutput) {
    const std::string output =
        "63d352ff004b\t0.12%\t1.129MiB / 128MiB\t0.88%\t84B / 84B\t0B / 0B\t2\n"
        "garbage line\n";
    auto stats = DockerContainerManager::parse_stats_output(output);
    ASSERT_EQ(stats.size(), 1u);
    const auto& s = stats.at("63d352ff004b");
    EXPECT_EQ(s.cpu, "0.12%");
    EXPECT_EQ(s.mem_usage, "1.129MiB / 128MiB");
    EXPECT_EQ(s.mem_percent, "0.88%");
    EXPECT_EQ(s.pids, "2");
}

TEST(DockerContainerManagerTest, ParsesInspectOutput) {
    const std::string output =
        "id\t63d352ff004be0429e8688904f8985ba\n"
        "name\t/web\n"
        "image\tnginx:1.27\n"
        "state\trunning\n"
        "entrypoint\tnull\n"
        "cmd\t[\"nginx\",\"-g\",\"daemon off;\"]\n"
        "nano_cpus\t500000000\n"
        "memory\t134217728\n"
        "memory_swap\t-1\n"
        "pids_limit\t<no value>\n"
        "restart_policy\ton-failure\n"
        "restart_retries\t3\n"
        "compose_project\t<no value>\n"
        "env\tAPP_ENV=dev\n"
        "env\tWITH_TAB=a\tb\n"
        "mount\t/srv/data\t/data\tfalse\n"
        "port\t80/tcp\t0.0.0.0:8080 \n"
        "port\t443/tcp\t\n"
        "network\tbridge\t172.17.0.2\n";

    auto d = DockerContainerManager::parse_inspect_output(output);
    EXPECT_EQ(d.name, "web");
    EXPECT_EQ(d.entrypoint, "");
    EXPECT_EQ(d.command, "[\"nginx\",\"-g\",\"daemon off;\"]");
    EXPECT_EQ(d.nano_cpus, 500000000);
    EXPECT_EQ(d.memory, 134217728);
    EXPECT_EQ(d.memory_swap, -1);
    EXPECT_EQ(d.pids_limit, 0);
    EXPECT_EQ(d.compose_project, "");
    ASSERT_EQ(d.env.size(), 2u);
    EXPECT_EQ(d.env[1], "WITH_TAB=a\tb");
    ASSERT_EQ(d.mounts.size(), 1u);
    EXPECT_EQ(d.mounts[0], "/srv/data -> /data (ro)");
    ASSERT_EQ(d.ports.size(), 2u);
    EXPECT_EQ(d.ports[0], "80/tcp -> 0.0.0.0:8080");
    EXPECT_EQ(d.ports[1], "443/tcp (not published)");
    ASSERT_EQ(d.networks.size(), 1u);
    EXPECT_EQ(d.networks[0], "bridge 172.17.0.2");

    auto settings = d.resource_settings();
    EXPECT_EQ(settings.cpus, "0.5");
    EXPECT_EQ(settings.memory, "128m");
    EXPECT_EQ(settings.memory_swap, "-1");
    EXPECT_EQ(settings.pids_limit, "");
    EXPECT_EQ(settings.restart_policy, "on-failure:3");
}

TEST(DockerContainerManagerTest, ListThrowsWithDaemonError) {
    auto executor = std::make_shared<FakeExecutor>();
    executor->next = {1, "Cannot connect to the Docker daemon\n", ""};
    DockerContainerManager manager(executor);
    EXPECT_THROW(manager.list_containers(), std::runtime_error);
    auto [ok, message] = manager.check_available();
    EXPECT_FALSE(ok);
    EXPECT_EQ(message, "Cannot connect to the Docker daemon");
}

TEST(DockerContainerManagerTest, ActionsQuoteIdsAndStripDaemonPrefix) {
    auto executor = std::make_shared<FakeExecutor>();
    DockerContainerManager manager(executor, "/usr/bin/docker");
    EXPECT_EQ(manager.runtime_name(), "docker");

    EXPECT_TRUE(manager.stop("abc").first);
    EXPECT_EQ(executor->commands.back(), "'/usr/bin/docker' stop 'abc'");

    executor->next = {1, "Error response from daemon: No such container: x'y\n", ""};
    auto [ok, message] = manager.start("x'y");
    EXPECT_FALSE(ok);
    EXPECT_EQ(message, "No such container: x'y");
    EXPECT_EQ(executor->commands.back(), "'/usr/bin/docker' start 'x'\\''y'");
}

TEST(DockerContainerManagerTest, ToggleStartsOrStopsByState) {
    auto executor = std::make_shared<FakeExecutor>();
    DockerContainerManager manager(executor);
    Container c;
    c.id = "abc";
    c.state = "exited";
    manager.toggle(c);
    EXPECT_EQ(executor->commands.back(), "'docker' start 'abc'");
    c.state = "paused";
    manager.toggle(c);
    EXPECT_EQ(executor->commands.back(), "'docker' stop 'abc'");
}

TEST(DockerContainerManagerTest, BuildsUpdateCommandWithOnlySetFlags) {
    auto executor = std::make_shared<FakeExecutor>();
    DockerContainerManager manager(executor);
    ResourceUpdate update;
    update.memory = "512m";
    update.restart_policy = "unless-stopped";
    EXPECT_EQ(manager.build_update_command("web", update),
              "'docker' update --memory '512m' --restart 'unless-stopped' 'web'");

    executor->next = {1, "invalid argument \"x\" for \"-m, --memory\" flag\nSee 'docker update --help'.\n\nUsage: ...\n", ""};
    auto [ok, message] = manager.update_resources("web", update);
    EXPECT_FALSE(ok);
    EXPECT_EQ(message, "invalid argument \"x\" for \"-m, --memory\" flag");

    executor->commands.clear();
    EXPECT_TRUE(manager.update_resources("web", ResourceUpdate{}).first);
    EXPECT_TRUE(executor->commands.empty());
}

TEST(DockerContainerManagerTest, BuildsLogsArgv) {
    DockerContainerManager manager(std::make_shared<FakeExecutor>());
    EXPECT_EQ(manager.build_logs_argv("web", 200, ""),
              (std::vector<std::string>{"docker", "logs", "-f", "--tail", "200", "web"}));
    EXPECT_EQ(manager.build_logs_argv("web", -1, "1730700000.5"),
              (std::vector<std::string>{"docker", "logs", "-f", "--tail", "all", "--since", "1730700000.5", "web"}));
}

TEST(ContainerFormatTest, FormatsFlagValues) {
    EXPECT_EQ(format_cpus(0), "");
    EXPECT_EQ(format_cpus(500000000), "0.5");
    EXPECT_EQ(format_cpus(2000000000), "2");
    EXPECT_EQ(format_cpus(1250000000), "1.25");
    EXPECT_EQ(format_memory_flag(0), "");
    EXPECT_EQ(format_memory_flag(1073741824), "1g");
    EXPECT_EQ(format_memory_flag(268435456), "256m");
    EXPECT_EQ(format_memory_flag(1536), "1536"); // 1.5KiB: no exact unit, plain bytes
    EXPECT_EQ(format_bytes(134217728), "128MiB");
    EXPECT_EQ(format_bytes(1610612736), "1.5GiB");
}

TEST(ResourceUpdateTest, SendsOnlyChangedFields) {
    ResourceSettings original{"0.5", "128m", "256m", "", "no"};
    ResourceSettings edited = original;
    edited.memory = " 512m ";
    edited.memory_swap = "1g";
    auto result = build_resource_update(original, edited);
    EXPECT_TRUE(result.error.empty());
    EXPECT_FALSE(result.update.cpus);
    EXPECT_EQ(result.update.memory.value_or(""), "512m");
    EXPECT_EQ(result.update.memory_swap.value_or(""), "1g");
    EXPECT_FALSE(result.update.pids_limit);
    EXPECT_FALSE(result.update.restart_policy);

    EXPECT_TRUE(build_resource_update(original, original).update.empty());
}

TEST(ResourceUpdateTest, ClearedFieldsMapToUnlimitedOrError) {
    ResourceSettings original{"1", "1g", "2g", "100", "always"};

    ResourceSettings edited = original;
    edited.pids_limit = "";
    edited.memory_swap = "";
    edited.restart_policy = "";
    auto result = build_resource_update(original, edited);
    EXPECT_TRUE(result.error.empty());
    EXPECT_EQ(result.update.pids_limit.value_or(""), "-1");
    EXPECT_EQ(result.update.memory_swap.value_or(""), "-1");
    EXPECT_EQ(result.update.restart_policy.value_or(""), "no");

    edited = original;
    edited.memory = "";
    EXPECT_FALSE(build_resource_update(original, edited).error.empty());
    edited = original;
    edited.cpus = "";
    EXPECT_FALSE(build_resource_update(original, edited).error.empty());
}

TEST(ProcessLogStreamTest, DeliversLinesFromChildProcess) {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> lines;
    {
        ProcessLogStream stream({"sh", "-c", "printf 'one\\ntwo\\nthree'"}, [&](std::string line) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(std::move(line));
            cv.notify_all();
        });
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return lines.size() == 3; });
    }
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "one");
    EXPECT_EQ(lines[2], "three");
}

TEST(ProcessLogStreamTest, CallsOnExitWhenProcessEnds) {
    std::mutex mutex;
    std::condition_variable cv;
    bool exited = false;
    ProcessLogStream stream({"true"}, [](std::string) {}, [&] {
        std::lock_guard<std::mutex> lock(mutex);
        exited = true;
        cv.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return exited; }));
}

TEST(ProcessLogStreamTest, DestructorStopsLongRunningProcess) {
    auto start = std::chrono::steady_clock::now();
    {
        ProcessLogStream stream({"sleep", "30"}, [](std::string) {});
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
}

TEST(DockerStatsNumbersTest, ParsesSizesPercentsAndPairs) {
    EXPECT_DOUBLE_EQ(parse_docker_size("84B"), 84.0);
    EXPECT_DOUBLE_EQ(parse_docker_size("1.5kB"), 1500.0);
    EXPECT_DOUBLE_EQ(parse_docker_size("2MiB"), 2.0 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(parse_docker_size("3GB"), 3e9);
    EXPECT_DOUBLE_EQ(parse_docker_size("12"), 12.0);
    EXPECT_LT(parse_docker_size("--"), 0);
    EXPECT_LT(parse_docker_size("5parsecs"), 0);
    EXPECT_DOUBLE_EQ(parse_docker_percent("12.5%"), 12.5);
    EXPECT_LT(parse_docker_percent("--"), 0);
    auto [rx, tx] = parse_docker_size_pair("1.1MiB / 128MiB");
    EXPECT_DOUBLE_EQ(rx, 1.1 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(tx, 128.0 * 1024 * 1024);
    EXPECT_LT(parse_docker_size_pair("n/a").first, 0);
}

} // namespace gsystemctl
