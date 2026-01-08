#pragma once

#include <string>

namespace gsystemctl {

struct ServiceUnit {
    std::string unit;
    std::string load;
    std::string active;
    std::string sub;
    std::string description;

    bool is_running() const { return sub == "running"; }
    bool is_loaded() const { return load == "loaded"; }
};

} // namespace gsystemctl
