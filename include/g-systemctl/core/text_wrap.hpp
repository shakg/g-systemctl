#pragma once

#include <string>
#include <vector>

namespace gsystemctl
{

    // Splits `line` into rows at most `width` terminal columns wide, breaking
    // anywhere (log lines are not prose; nothing is dropped or trimmed).
    // Wide glyphs are never split. width <= 0 returns the line unchanged.
    std::vector<std::string> wrap_text(const std::string &line, int width);

} // namespace gsystemctl
