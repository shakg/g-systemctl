#pragma once

#include "g-systemctl/core/logging_manager.hpp"
#include <memory>

namespace gsystemctl {

class MacOSLoggingManager : public LoggingManager {
public:
    explicit MacOSLoggingManager(std::shared_ptr<CommandExecutor> executor);

    std::pair<bool, std::string> open_logs(const std::string& unit) override;

private:
    std::shared_ptr<CommandExecutor> executor_;
};

} // namespace gsystemctl
