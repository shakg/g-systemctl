#pragma once

#include "g-systemctl/container/container.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gsystemctl {

// Container runtime operations. Implementations must be safe to call from a
// background thread; they hold no mutable state.
class ContainerManager {
public:
    using Result = std::pair<bool, std::string>;

    virtual ~ContainerManager() = default;

    // Short name for the UI, e.g. "docker" or "podman".
    virtual std::string runtime_name() const = 0;
    // What to try when check_available() fails.
    virtual std::string setup_hint() const = 0;
    // {true, server version} or {false, reason}.
    virtual Result check_available() = 0;

    // All containers, including stopped ones. Throws on failure.
    virtual std::vector<Container> list_containers() = 0;
    // One sample per running container, keyed by short ID. Blocks for about
    // a second while the runtime measures CPU. Throws on failure.
    virtual std::map<std::string, ContainerStats> container_stats() = 0;
    // Throws on failure.
    virtual ContainerDetails inspect(const std::string& id) = 0;

    virtual Result start(const std::string& id) = 0;
    virtual Result stop(const std::string& id) = 0;
    virtual Result restart(const std::string& id) = 0;
    Result toggle(const Container& container) {
        return container.is_active() ? stop(container.id) : start(container.id);
    }
    // Applies limits to a running or stopped container without recreating it.
    virtual Result update_resources(const std::string& id, const ResourceUpdate& update) = 0;

    // Follows the container's logs, starting with the last `tail` lines
    // (tail < 0: all). A non-empty `since` (unix timestamp) skips older lines.
    // The runtime stops following when the container stops; `on_end` runs on
    // a background thread when that happens or the stream is destroyed.
    virtual std::unique_ptr<LogStream> stream_logs(const std::string& id, int tail, const std::string& since,
                                                   LoggingManager::LogCallback on_line,
                                                   std::function<void()> on_end) = 0;

    // ---- compose ----
    // Detects the compose command (`docker compose`, `docker-compose`, `podman compose`,
    // `podman-compose`). {true, "docker compose 2.29.1"} or {false, reason}.
    virtual Result compose_available() = 0;
    // Whether compose_config_hashes() can work: podman-compose has no `config --hash`.
    virtual bool compose_drift_supported() = 0;
    // Runs `compose -p NAME -f FILE... <action>` from the project's working directory.
    // Start/stop/restart/down fall back to `-p NAME` alone if the files are gone.
    virtual Result compose(const ComposeProjectRef& project, ComposeAction action) = 0;
    // Project name the files resolve to (top-level `name:`, else the directory name),
    // via `compose config`. `project.name` is ignored. Throws on failure, e.g. invalid file.
    virtual std::string compose_project_name(const ComposeProjectRef& project) = 0;
    // Service -> config hash of the project's files as they are now. Throws on failure.
    virtual std::map<std::string, std::string> compose_config_hashes(const ComposeProjectRef& project) = 0;
    // The command stream_compose_logs() runs, for the log header.
    virtual std::string compose_logs_command(const std::string& project, int tail) const = 0;
    // Follows the merged logs of all the project's services, prefixed with the
    // container name. Ends when every container has stopped. `containers` are
    // the project's containers as last listed; podman follows them directly.
    virtual std::unique_ptr<LogStream> stream_compose_logs(const ComposeProjectRef& project,
                                                           const std::vector<std::string>& containers, int tail,
                                                           const std::string& since,
                                                           LoggingManager::LogCallback on_line,
                                                           std::function<void()> on_end) = 0;

    // $GSYSTEMCTL_RUNTIME picks docker or podman (default: auto, docker first);
    // $GSYSTEMCTL_DOCKER overrides the CLI binary.
    static std::unique_ptr<ContainerManager> create(std::shared_ptr<CommandExecutor> executor);
};

} // namespace gsystemctl
