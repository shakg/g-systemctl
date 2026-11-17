#pragma once

#include <string>
#include <vector>

namespace gsystemctl
{

    // Splits `line` into rows at most `width` terminal columns wide, breaking
    // anywhere (log lines are not prose; nothing is dropped or trimmed).
    // Wide glyphs are never split. width <= 0 returns the line unchanged.
    std::vector<std::string> wrap_text(const std::string &line, int width);

    // Wraps prose at spaces into rows at most `width` columns wide. A word
    // longer than a row is broken (wrap_text), so nothing is clipped.
    // Runs of spaces collapse at row breaks. width <= 0 returns {text}.
    std::vector<std::string> wrap_words(const std::string &text, int width);

    // Cuts `text` to `width` columns, ending in "…" when something was cut.
    std::string ellipsize(const std::string &text, int width);

} // namespace gsystemctl
