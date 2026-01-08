#include "g-systemctl/platform/platform.hpp"

namespace gsystemctl {

Platform detect_platform() {
#if defined(__linux__)
    return Platform::Linux;
#elif defined(__APPLE__) && defined(__MACH__)
    return Platform::MacOS;
#else
    return Platform::Unknown;
#endif
}

const char* platform_name(Platform p) {
    switch (p) {
        case Platform::Linux: return "Linux";
        case Platform::MacOS: return "macOS";
        default: return "Unknown";
    }
}

} // namespace gsystemctl
