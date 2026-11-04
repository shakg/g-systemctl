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

    // Short name for the UI, e.g. "docker".
    virtual std::string runtime_name() const = 0;
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

    // Uses $GSYSTEMCTL_DOCKER as the CLI binary if set, otherwise "docker".
    static std::unique_ptr<ContainerManager> create(std::shared_ptr<CommandExecutor> executor);
};

} // namespace gsystemctl
