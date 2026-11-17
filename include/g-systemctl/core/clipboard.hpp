#pragma once

#include <string>
#include <vector>

namespace gsystemctl
{

    // A position in a list of log lines: line index and terminal column.
    struct TextPoint
    {
        int line = 0;
        int col = 0;
    };

    // Columns [from, to) of `line`. A wide glyph is kept when its first column is inside.
    std::string slice_columns(const std::string &line, int from, int to);

    // Text from `a` to `b`, both cells included, in either order. Lines are
    // joined with '\n'; wrapping is not part of the text, so a pasted copy
    // matches the original log lines.
    std::string selected_text(const std::vector<std::string> &lines, TextPoint a, TextPoint b);

    // The last `count` lines joined with '\n'.
    std::string last_lines(const std::vector<std::string> &lines, size_t count);

    std::string base64_encode(const std::string &data);

    // OSC 52 "set clipboard" sequence. `tmux_passthrough` wraps it in tmux's
    // DCS passthrough (needs `allow-passthrough on`).
    std::string osc52_sequence(const std::string &text, bool tmux_passthrough);

    // Puts `text` on the clipboard: inside tmux through `tmux load-buffer -w`
    // (tmux forwards it to the outer terminal when set-clipboard allows),
    // otherwise OSC 52 to the terminal plus wl-copy / xclip / xsel / pbcopy
    // when present. Returns how it was sent, empty when nothing worked.
    std::string copy_to_clipboard(const std::string &text);

} // namespace gsystemctl
