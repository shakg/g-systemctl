#pragma once

#include "g-systemctl/container/container_manager.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace gsystemctl {

// Which CLI dialect to speak. Podman's CLI is close to Docker's; the
// differences are a few template fields, the version check, compose
// providers and pod infra containers.
enum class ContainerRuntime { Docker, Podman };

// Drives the docker or podman CLI with Go-template output formats, so no JSON
// parser is needed and Docker Desktop / podman machine on macOS work the same
// as Linux. The only state is the compose command, detected once on first use.
class CliContainerManager : public ContainerManager {
public:
    CliContainerManager(std::shared_ptr<CommandExecutor> executor, std::string binary = "docker",
                        ContainerRuntime runtime = ContainerRuntime::Docker);

    std::string runtime_name() const override;
    std::string setup_hint() const override;
    Result check_available() override;
    std::vector<Container> list_containers() override;
    std::map<std::string, ContainerStats> container_stats() override;
    ContainerDetails inspect(const std::string& id) override;
    Result start(const std::string& id) override;
    Result stop(const std::string& id) override;
    Result restart(const std::string& id) override;
    Result update_resources(const std::string& id, const ResourceUpdate& update) override;
    std::unique_ptr<LogStream> stream_logs(const std::string& id, int tail, const std::string& since,
                                           LoggingManager::LogCallback on_line,
                                           std::function<void()> on_end) override;

    Result compose_available() override;
    bool compose_drift_supported() override;
    Result compose(const ComposeProjectRef& project, ComposeAction action) override;
    std::string compose_project_name(const ComposeProjectRef& project) override;
    std::map<std::string, std::string> compose_config_hashes(const ComposeProjectRef& project) override;
    std::unique_ptr<LogStream> stream_compose_logs(const ComposeProjectRef& project, int tail, const std::string& since,
                                                   LoggingManager::LogCallback on_line,
                                                   std::function<void()> on_end) override;

    struct Selection {
        std::string binary;
        ContainerRuntime runtime;
    };
    // Picks the CLI. `runtime_env` ($GSYSTEMCTL_RUNTIME): "docker", "podman" or
    // empty/"auto". `binary_env` ($GSYSTEMCTL_DOCKER) overrides the binary. In
    // auto mode the runtime is read from `<binary> --version`, so a `docker`
    // that is really podman (podman-docker) gets the podman dialect. Without a
    // binary override docker is preferred, unless its daemon is unreachable and
    // podman is installed.
    static Selection select_runtime(CommandExecutor& executor, const std::string& runtime_env,
                                    const std::string& binary_env);

    // Exposed for tests.
    ContainerRuntime runtime() const { return runtime_; }
    // `compose` is the command prefix, e.g. {"docker", "compose"}; `with_files`: pass -f and cd.
    // An empty project name leaves out -p.
    static std::string build_compose_command(const std::vector<std::string>& compose, const ComposeProjectRef& project,
                                             const std::vector<std::string>& args, bool with_files);
    // The `name:` line of `compose config` output.
    static std::string parse_config_project_name(const std::string& output);
    static std::vector<std::string> build_compose_logs_argv(const std::vector<std::string>& compose,
                                                            const ComposeProjectRef& project, int tail,
                                                            const std::string& since, bool with_files = false);
    // Rows whose 15th field is "true" (podman pod infra containers) are dropped.
    static std::vector<Container> parse_ps_output(const std::string& output);
    static std::map<std::string, ContainerStats> parse_stats_output(const std::string& output);
    static ContainerDetails parse_inspect_output(const std::string& output);
    // First line of a failed command, without the CLI's error prefix.
    static std::string describe_failure(const CommandResult& result);
    std::string build_update_command(const std::string& id, const ResourceUpdate& update) const;
    std::vector<std::string> build_logs_argv(const std::string& id, int tail, const std::string& since) const;
    // Podman 5 has `{{.Label "k"}}`; podman 4 only `{{index .Labels "k"}}`, where
    // podman 5's `.Labels` is a string. list_containers() switches on the error.
    std::string ps_command(bool labels_index = false) const;
    std::string stats_command() const;
    std::string inspect_command(const std::string& id) const;
    // `podman compose` banner ">>>> Executing external compose provider ...".
    static bool is_compose_banner(const std::string& line);
    static std::string strip_compose_banner(const std::string& text);
    // Drops ANSI CSI sequences ("ESC[0m").
    static std::string strip_ansi(const std::string& line);
    // The line of a failed compose command that says why.
    static std::string compose_error(const std::string& output);
    std::string version_command() const;
    // Compose commands tried in order when $GSYSTEMCTL_COMPOSE is unset.
    std::vector<std::vector<std::string>> compose_candidates() const;

private:
    Result run_simple(const std::string& args);
    // Runs a compose command; drops the podman compose banner from its output.
    CommandResult run_compose(const std::string& command);
    // Resolves the compose command once; empty if none works.
    const std::vector<std::string>& compose_prefix();

    std::once_flag compose_once_;
    std::vector<std::string> compose_prefix_;
    std::string compose_version_;
    std::string compose_error_;
    bool compose_hash_ = false;
    std::atomic<bool> labels_index_{false};

    std::shared_ptr<CommandExecutor> executor_;
    std::string binary_;
    ContainerRuntime runtime_;
};

} // namespace gsystemctl
