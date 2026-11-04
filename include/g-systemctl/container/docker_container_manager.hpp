#pragma once

#include "g-systemctl/container/container_manager.hpp"
#include <memory>
#include <string>

namespace gsystemctl {

// Drives the docker CLI with Go-template output formats, so no JSON parser
// is needed and Docker Desktop on macOS works the same as Linux.
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
    std::unique_ptr<LogStream> stream_logs(const std::string& id, int tail,
                                           LoggingManager::LogCallback on_line) override;

    // Exposed for tests.
    static std::vector<Container> parse_ps_output(const std::string& output);
    static std::map<std::string, ContainerStats> parse_stats_output(const std::string& output);
    static ContainerDetails parse_inspect_output(const std::string& output);
    std::string build_update_command(const std::string& id, const ResourceUpdate& update) const;

private:
    Result run_simple(const std::string& args);

    std::shared_ptr<CommandExecutor> executor_;
    std::string binary_;
};

} // namespace gsystemctl
