#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gsystemctl {

struct ServiceUnit {
    std::string unit;
    std::string load;
    std::string active;
    std::string sub;
    std::string description;
    std::string pid;
    std::string cpu;
    std::string memory;

    bool is_running() const { return sub == "running"; }
    bool is_loaded() const { return load == "loaded"; }
};

// Label/value rows for a service's detail page, in display order.
using ServiceDetails = std::vector<std::pair<std::string, std::string>>;

} // namespace gsystemctl
