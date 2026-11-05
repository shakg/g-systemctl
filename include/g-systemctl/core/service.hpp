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

// Raw resource counters for the detail page graphs. -1 = not available.
struct ServiceSample {
    double cpu_nsec = -1;       // total CPU time used, nanoseconds
    double memory_bytes = -1;
    double tasks = -1;
    double io_read_bytes = -1;  // totals; need IOAccounting=yes
    double io_write_bytes = -1;

    bool empty() const {
        return cpu_nsec < 0 && memory_bytes < 0 && tasks < 0 && io_read_bytes < 0 && io_write_bytes < 0;
    }
};

} // namespace gsystemctl
