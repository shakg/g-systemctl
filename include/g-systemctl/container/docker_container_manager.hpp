#pragma once

#include "g-systemctl/container/container_manager.hpp"
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace gsystemctl {

// Drives the docker CLI with Go-template output formats, so no JSON parser
// is needed and Docker Desktop on macOS works the same as Linux. The only
// state is the compose command, detected once on first use.
class DockerContainerManager : public ContainerManager {
public:
    explicit DockerContainerManager(std::shared_ptr<CommandExecutor> executor, std::string binary = "docker");

    std::string runtime_name() const override;
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
    Result compose(const ComposeProjectRef& project, ComposeAction action) override;
    std::string compose_project_name(const ComposeProjectRef& project) override;
    std::map<std::string, std::string> compose_config_hashes(const ComposeProjectRef& project) override;
    std::unique_ptr<LogStream> stream_compose_logs(const ComposeProjectRef& project, int tail, const std::string& since,
                                                   LoggingManager::LogCallback on_line,
                                                   std::function<void()> on_end) override;

    // Exposed for tests.
    // `compose` is the command prefix, e.g. {"docker", "compose"}; `with_files`: pass -f and cd.
    // An empty project name leaves out -p.
    static std::string build_compose_command(const std::vector<std::string>& compose, const ComposeProjectRef& project,
                                             const std::vector<std::string>& args, bool with_files);
    // The `name:` line of `compose config` output.
    static std::string parse_config_project_name(const std::string& output);
    static std::vector<std::string> build_compose_logs_argv(const std::vector<std::string>& compose,
                                                            const ComposeProjectRef& project, int tail,
                                                            const std::string& since);
    static std::vector<Container> parse_ps_output(const std::string& output);
    static std::map<std::string, ContainerStats> parse_stats_output(const std::string& output);
    static ContainerDetails parse_inspect_output(const std::string& output);
    std::string build_update_command(const std::string& id, const ResourceUpdate& update) const;
    std::vector<std::string> build_logs_argv(const std::string& id, int tail, const std::string& since) const;

private:
    Result run_simple(const std::string& args);
    // Resolves the compose command once; empty if none works.
    const std::vector<std::string>& compose_prefix();

    std::once_flag compose_once_;
    std::vector<std::string> compose_prefix_;
    std::string compose_version_;
    std::string compose_error_;

    std::shared_ptr<CommandExecutor> executor_;
    std::string binary_;
};

} // namespace gsystemctl
