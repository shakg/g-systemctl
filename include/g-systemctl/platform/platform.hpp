#pragma once

namespace gsystemctl {

enum class Platform {
    Linux,
    MacOS,
    Unknown
};

Platform detect_platform();
const char* platform_name(Platform p);

} // namespace gsystemctl
