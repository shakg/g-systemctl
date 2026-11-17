#include "g-systemctl/core/clipboard.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ftxui/screen/string.hpp>
#include <iostream>
#include <unistd.h>

namespace gsystemctl
{

    std::string slice_columns(const std::string &line, int from, int to)
    {
        std::string out;
        int col = 0;
        for (const auto &glyph : ftxui::Utf8ToGlyphs(line))
        {
            if (glyph.empty())
                continue;
            const int w = std::max(0, ftxui::string_width(glyph));
            if (col >= to)
                break;
            if (col >= from)
                out += glyph;
            col += w;
        }
        return out;
    }

    std::string selected_text(const std::vector<std::string> &lines, TextPoint a, TextPoint b)
    {
        if (lines.empty())
            return {};
        if (b.line < a.line || (b.line == a.line && b.col < a.col))
            std::swap(a, b);
        const int last = static_cast<int>(lines.size()) - 1;
        a.line = std::clamp(a.line, 0, last);
        b.line = std::clamp(b.line, 0, last);
        std::string out;
        for (int i = a.line; i <= b.line; ++i)
        {
            const int from = i == a.line ? std::max(0, a.col) : 0;
            const int to = i == b.line ? b.col + 1 : 1 << 30;
            if (i > a.line)
                out += '\n';
            out += slice_columns(lines[i], from, to);
        }
        return out;
    }

    std::string last_lines(const std::vector<std::string> &lines, size_t count)
    {
        const size_t start = lines.size() > count ? lines.size() - count : 0;
        std::string out;
        for (size_t i = start; i < lines.size(); ++i)
        {
            if (i > start)
                out += '\n';
            out += lines[i];
        }
        return out;
    }

    std::string base64_encode(const std::string &data)
    {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3)
        {
            const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8) | static_cast<unsigned char>(data[i + 2]);
            out += table[(v >> 18) & 63];
            out += table[(v >> 12) & 63];
            out += table[(v >> 6) & 63];
            out += table[v & 63];
        }
        if (i < data.size())
        {
            unsigned v = static_cast<unsigned char>(data[i]) << 16;
            if (i + 1 < data.size())
                v |= static_cast<unsigned char>(data[i + 1]) << 8;
            out += table[(v >> 18) & 63];
            out += table[(v >> 12) & 63];
            out += i + 1 < data.size() ? table[(v >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    std::string osc52_sequence(const std::string &text, bool tmux_passthrough)
    {
        const std::string osc = "\x1b]52;c;" + base64_encode(text) + "\x07";
        if (!tmux_passthrough)
            return osc;
        // Inside DCS passthrough every ESC is doubled.
        std::string wrapped = "\x1bPtmux;";
        for (char c : osc)
        {
            if (c == '\x1b')
                wrapped += '\x1b';
            wrapped += c;
        }
        return wrapped + "\x1b\\";
    }

    namespace
    {
        bool pipe_to(const std::string &command, const std::string &text)
        {
            FILE *pipe = popen((command + " >/dev/null 2>&1").c_str(), "w");
            if (!pipe)
                return false;
            fwrite(text.data(), 1, text.size(), pipe);
            return pclose(pipe) == 0;
        }

        bool have(const char *program)
        {
            return std::system((std::string("command -v ") + program + " >/dev/null 2>&1").c_str()) == 0;
        }

        bool env_set(const char *name)
        {
            const char *v = std::getenv(name);
            return v && *v;
        }

        void write_terminal(const std::string &sequence)
        {
            std::cout << sequence << std::flush;
        }
    } // namespace

    std::string copy_to_clipboard(const std::string &text)
    {
        if (env_set("TMUX"))
        {
            // -w (tmux 3.2+) also sets the outer terminal's clipboard.
            if (pipe_to("tmux load-buffer -w -", text))
                return "tmux buffer + clipboard";
            write_terminal(osc52_sequence(text, true));
            if (pipe_to("tmux load-buffer -", text))
                return "tmux buffer";
            return "terminal (OSC 52)";
        }

        write_terminal(osc52_sequence(text, false));
        std::string via = "terminal (OSC 52)";
#ifdef __APPLE__
        if (pipe_to("pbcopy", text))
            via = "pbcopy";
#else
        if (env_set("WAYLAND_DISPLAY") && have("wl-copy") && pipe_to("wl-copy", text))
            via = "wl-copy";
        else if (env_set("DISPLAY") && have("xclip") && pipe_to("xclip -selection clipboard", text))
            via = "xclip";
        else if (env_set("DISPLAY") && have("xsel") && pipe_to("xsel --clipboard --input", text))
            via = "xsel";
#endif
        return via;
    }

} // namespace gsystemctl
