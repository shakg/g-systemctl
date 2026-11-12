#include <gtest/gtest.h>
#include <g-systemctl/container/cli_container_manager.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gsystemctl {

namespace {

// Answers each command with the result of the longest matching prefix;
// unmatched commands fail with 127 (not found).
class ScriptedExecutor : public CommandExecutor {
public:
    std::map<std::string, CommandResult> script;
    std::vector<std::string> commands;

    CommandResult execute(const std::string& command) override {
        commands.push_back(command);
        const CommandResult* best = nullptr;
        size_t best_len = 0;
        for (const auto& [prefix, result] : script) {
            if (command.rfind(prefix, 0) == 0 && prefix.size() >= best_len) {
                best = &result;
                best_len = prefix.size();
            }
        }
        return best ? *best : CommandResult{127, "", "not found"};
    }
    CommandResult execute_privileged(const std::string& command) override { return execute(command); }
    CommandResult execute_privileged(const std::string& command, const std::string&) override { return execute(command); }
    bool can_execute_privileged_without_password() override { return true; }
    int execute_interactive(const std::string&, bool) override { return 0; }
};

} // namespace

TEST(RuntimeSelectionTest, ExplicitRuntimeSkipsProbing) {
    ScriptedExecutor executor;
    auto s = CliContainerManager::select_runtime(executor, "Podman", "");
    EXPECT_EQ(s.binary, "podman");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);
    s = CliContainerManager::select_runtime(executor, "docker", "/opt/bin/docker");
    EXPECT_EQ(s.binary, "/opt/bin/docker");
    EXPECT_EQ(s.runtime, ContainerRuntime::Docker);
    EXPECT_TRUE(executor.commands.empty());
}

TEST(RuntimeSelectionTest, AutoPrefersDockerThenPodman) {
    ScriptedExecutor executor;
    executor.script["'docker' --version"] = {0, "Docker version 27.3.1, build ce12230\n", ""};
    executor.script["'podman' --version"] = {0, "podman version 5.2.5\n", ""};
    executor.script["docker version"] = {0, "27.3.1\n", ""};
    auto s = CliContainerManager::select_runtime(executor, "", "");
    EXPECT_EQ(s.binary, "docker");
    EXPECT_EQ(s.runtime, ContainerRuntime::Docker);

    // Docker CLI installed but its daemon is down.
    executor.script["docker version"] = {1, "", "Cannot connect to the Docker daemon\n"};
    s = CliContainerManager::select_runtime(executor, "", "");
    EXPECT_EQ(s.binary, "podman");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);

    executor.script.erase("'docker' --version");
    s = CliContainerManager::select_runtime(executor, "auto", "");
    EXPECT_EQ(s.binary, "podman");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);
}

TEST(RuntimeSelectionTest, DetectsPodmanDockerShim) {
    ScriptedExecutor executor;
    executor.script["'docker' --version"] = {0, "podman version 5.2.5\n", ""};
    auto s = CliContainerManager::select_runtime(executor, "", "");
    EXPECT_EQ(s.binary, "docker");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);
}

TEST(RuntimeSelectionTest, BinaryOverrideIsProbed) {
    ScriptedExecutor executor;
    executor.script["'/usr/local/bin/podman-remote' --version"] = {0, "podman-remote version 5.0.0\n", ""};
    auto s = CliContainerManager::select_runtime(executor, "", "/usr/local/bin/podman-remote");
    EXPECT_EQ(s.binary, "/usr/local/bin/podman-remote");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);

    // Not runnable: guess from the name, check_available() reports the error.
    s = CliContainerManager::select_runtime(executor, "", "/missing/podman");
    EXPECT_EQ(s.runtime, ContainerRuntime::Podman);
    s = CliContainerManager::select_runtime(executor, "", "/missing/docker");
    EXPECT_EQ(s.runtime, ContainerRuntime::Docker);
}

TEST(RuntimeSelectionTest, FallsBackToDockerWhenNothingIsInstalled) {
    ScriptedExecutor executor;
    auto s = CliContainerManager::select_runtime(executor, "", "");
    EXPECT_EQ(s.binary, "docker");
    EXPECT_EQ(s.runtime, ContainerRuntime::Docker);
}

TEST(PodmanDialectTest, CommandsUsePodmanFieldNames) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager podman(executor, "podman", ContainerRuntime::Podman);
    CliContainerManager docker(executor, "docker", ContainerRuntime::Docker);

    EXPECT_NE(podman.stats_command().find("{{.PIDS}}"), std::string::npos);
    EXPECT_EQ(podman.stats_command().find("{{.PIDs}}"), std::string::npos);
    EXPECT_NE(docker.stats_command().find("{{.PIDs}}"), std::string::npos);

    EXPECT_NE(podman.ps_command().find("{{.IsInfra}}"), std::string::npos);
    EXPECT_EQ(docker.ps_command().find("IsInfra"), std::string::npos);

    EXPECT_NE(podman.inspect_command("x").find("{{.HostIP}}"), std::string::npos);
    EXPECT_NE(docker.inspect_command("x").find("{{.HostIp}}"), std::string::npos);

    EXPECT_EQ(podman.version_command(), "'podman' info --format '{{.Version.Version}}'");
    EXPECT_EQ(docker.version_command(), "'docker' version --format '{{.Server.Version}}'");

    EXPECT_EQ(podman.build_logs_argv("web", -1, ""), (std::vector<std::string>{"podman", "logs", "-f", "web"}));
    EXPECT_EQ(podman.build_logs_argv("web", 50, ""),
              (std::vector<std::string>{"podman", "logs", "-f", "--tail", "50", "web"}));
}

TEST(PodmanDialectTest, CheckAvailableReadsInfoVersion) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    executor->script["'podman' info"] = {0, "5.2.5\n", ""};
    EXPECT_EQ(manager.check_available(), (ContainerManager::Result{true, "5.2.5"}));

    executor->script["'podman' info"] = {
        125, "", "Error: unable to connect to Podman socket: failed to connect: dial tcp 127.0.0.1:50000\n"};
    EXPECT_EQ(manager.check_available(),
              (ContainerManager::Result{false, "unable to connect to Podman socket: failed to connect: dial tcp "
                                               "127.0.0.1:50000"}));
}

TEST(PodmanDialectTest, PsDropsPodInfraContainers) {
    const std::string labels(9, '\t');
    const std::string output =
        "aaaaaaaaaaaa\tweb\tnginx\trunning\tUp 2 minutes\t\tshop\tweb\t1\t/srv/shop\t/srv/shop/compose.yaml\t\t\t\tfalse\n"
        "bbbbbbbbbbbb\t1f2e3d4c5b6a-infra\tlocalhost/podman-pause:5.2.5\trunning\tUp 2 minutes\t" + labels + "true\n";
    auto containers = CliContainerManager::parse_ps_output(output);
    ASSERT_EQ(containers.size(), 1u);
    EXPECT_EQ(containers[0].name, "web");
    EXPECT_EQ(containers[0].compose_project, "shop");
    EXPECT_EQ(containers[0].compose_number, 1);
}

TEST(PodmanDialectTest, FallsBackToLabelsIndexOnPodman4) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    executor->script["'podman' ps -a --format '{{.ID}}"] = {
        125, "aaaaaaaaaaaa\tweb", "Error: template: ps:1:77: executing \"ps\" at <.Label>: can't evaluate field Label\n"};
    executor->script["'podman' ps -a --format '{{.ID}}\t{{.Names}}\t{{.Image}}\t{{.State}}\t{{.Status}}\t{{.Ports}}\t{{index"] = {
        0, "aaaaaaaaaaaa\tweb\tnginx\trunning\tUp\t\tshop\n", ""};
    auto containers = manager.list_containers();
    ASSERT_EQ(containers.size(), 1u);
    EXPECT_EQ(containers[0].compose_project, "shop");
    EXPECT_EQ(executor->commands.size(), 2u);
    EXPECT_EQ(executor->commands[1].find("{{.Label "), std::string::npos);
    EXPECT_NE(executor->commands[1].find("{{index .Labels \"com.docker.compose.project\"}}"), std::string::npos);

    // Remembered: the next poll goes straight to the podman 4 format.
    manager.list_containers();
    EXPECT_EQ(executor->commands.size(), 3u);
    EXPECT_NE(executor->commands[2].find("{{index .Labels "), std::string::npos);
}

TEST(PodmanDialectTest, ParsesPodman4Inspect) {
    auto d = CliContainerManager::parse_inspect_output(
        "name\tsolo\nentrypoint\t\"\"\ncmd\t[\"sleep\",\"3600\"]\nrestart_policy\t\n"
        "port\t80/tcp\t:8089 \nnetwork\tpodman\t10.88.0.3\n");
    EXPECT_EQ(d.name, "solo");
    EXPECT_EQ(d.entrypoint, "");
    EXPECT_EQ(d.command, "[\"sleep\",\"3600\"]");
    ASSERT_EQ(d.ports.size(), 1u);
    EXPECT_EQ(d.ports[0], "80/tcp -> :8089");
    EXPECT_EQ(d.resource_settings().restart_policy, "no");
}

TEST(PodmanDialectTest, ParsesPodmanStats) {
    auto stats = CliContainerManager::parse_stats_output("aaaaaaaaaaaa\t1.25%\t12.3MB / 8.2GB\t0.15%\t1.2kB / 648B\t0B / 4.1kB\t3\n");
    ASSERT_EQ(stats.size(), 1u);
    const auto& s = stats.at("aaaaaaaaaaaa");
    EXPECT_DOUBLE_EQ(parse_docker_percent(s.cpu), 1.25);
    EXPECT_DOUBLE_EQ(parse_docker_size_pair(s.mem_usage).first, 12.3e6);
    EXPECT_DOUBLE_EQ(parse_docker_size(s.pids), 3);
}

TEST(PodmanDialectTest, StripsPodmanErrorPrefix) {
    EXPECT_EQ(CliContainerManager::describe_failure(
                  {125, "", "Error: no container with name or ID \"x\" found: no such container\n"}),
              "no container with name or ID \"x\" found: no such container");
}

TEST(PodmanComposeTest, PrefersPodmanComposeWrapperWithHashSupport) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    executor->script["'podman' 'compose' 'version'"] = {0, "2.29.7\n", ""};
    executor->script["'podman' 'compose' 'config' '--help'"] = {0, "Options:\n      --hash string   Print the service config hash\n", ""};
    EXPECT_EQ(manager.compose_available(), (ContainerManager::Result{true, "podman compose 2.29.7"}));
    EXPECT_TRUE(manager.compose_drift_supported());
}

TEST(PodmanComposeTest, DropsProviderBanner) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    const std::string banner =
        "\x1b[4m>>>> Executing external compose provider \"/usr/libexec/docker/cli-plugins/docker-compose\". "
        "Please refer to the documentation for details. <<<<\n\n\x1b[0m";
    executor->script["'podman' 'compose' 'version'"] = {0, banner + "2.29.7\n", ""};
    executor->script["'podman' 'compose' 'config' '--help'"] = {0, banner + "      --hash string\n", ""};
    EXPECT_EQ(CliContainerManager::strip_compose_banner(banner + "name: shop\nservices:\n"), "name: shop\nservices:\n");
    EXPECT_EQ(manager.compose_available(), (ContainerManager::Result{true, "podman compose 2.29.7"}));
    executor->script["'podman' 'compose' '-p' 'shop' 'stop'"] = {1, banner, "no such service: web\n"};
    ComposeProjectRef ref;
    ref.name = "shop";
    EXPECT_EQ(manager.compose(ref, ComposeAction::Stop), (ContainerManager::Result{false, "no such service: web"}));
}

TEST(PodmanComposeTest, FallsBackToPodmanComposeWithoutDrift) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    executor->script["'podman' 'compose' 'version'"] = {125, "", "Error: looking up compose provider failed\n"};
    executor->script["'podman-compose' 'version'"] = {0, "1.6.0\npodman version 5.2.5\n", ""};
    executor->script["'podman-compose' 'config' '--help'"] = {0, "usage: podman-compose config [--no-normalize] [--services]\n", ""};
    EXPECT_EQ(manager.compose_available(), (ContainerManager::Result{true, "podman-compose 1.6.0"}));
    EXPECT_FALSE(manager.compose_drift_supported());
    // docker-compose would talk to the Docker daemon, so it is never tried.
    for (const auto& command : executor->commands) {
        EXPECT_EQ(command.find("docker-compose"), std::string::npos) << command;
    }
}

TEST(PodmanComposeTest, ReportsAllCandidatesWhenNoneWork) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "podman", ContainerRuntime::Podman);
    executor->script["'podman' 'compose' 'version'"] = {125, "", "Error: looking up compose provider failed\n"};
    auto [ok, message] = manager.compose_available();
    EXPECT_FALSE(ok);
    EXPECT_EQ(message, "podman compose or podman-compose is not installed (looking up compose provider failed)");
    EXPECT_FALSE(manager.compose_drift_supported());
}

TEST(DockerComposeTest, DetectsHashSupport) {
    auto executor = std::make_shared<ScriptedExecutor>();
    CliContainerManager manager(executor, "docker", ContainerRuntime::Docker);
    executor->script["'docker' 'compose' 'version'"] = {0, "2.29.1\n", ""};
    executor->script["'docker' 'compose' 'config' '--help'"] = {0, "      --hash string   Print the service config hash, one per line.\n", ""};
    EXPECT_EQ(manager.compose_available(), (ContainerManager::Result{true, "docker compose 2.29.1"}));
    EXPECT_TRUE(manager.compose_drift_supported());
}

} // namespace gsystemctl
