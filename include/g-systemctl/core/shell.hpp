#pragma once

#include <string>

namespace gsystemctl {

// Wraps a value in single quotes for safe use in a /bin/sh command line.
inline std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    return quoted + "'";
}

} // namespace gsystemctl
