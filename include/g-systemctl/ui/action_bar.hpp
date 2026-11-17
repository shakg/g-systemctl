#pragma once

#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>
#include <string>

namespace gsystemctl::ui
{

    // Clickable buttons on the detail page: Back (left) and Start / Stop / Restart
    // (right) in the title bar; Shell (containers), Graphs and Expand / Shrink in the log
    // header. Each has a keyboard shortcut.
    class ActionBar
    {
    public:
        enum class Action
        {
            None,
            Start,
            Stop,
            Restart,
            Back,
            ToggleLogs,
            ToggleGraphs,
            Shell,
        };

        // Call before render()/render_back() each frame: picks how much of the
        // title-bar buttons to draw so `name` (up to kNameRoom cells) still fits
        // on the terminal: full ("▶ Start Alt+o"), without keys ("▶ Start"),
        // or glyphs only ("▶").
        void fit_title(const std::string &name, const std::string &state_label)
        {
            const int room = ftxui::Terminal::Size().dimx - 2; // inside the page border
            const int name_width = std::min(ftxui::string_width(name), kNameRoom);
            for (level_ = 0; level_ < 2; ++level_)
            {
                // " " + Back + name + " " + state + "  " + Start Stop Restart
                const int need = 1 + button_width(kBack) + name_width + 1 + ftxui::string_width(state_label) + 2 +
                                 button_width(kStart) + button_width(kStop) + button_width(kRestart);
                if (need <= room)
                    return;
            }
        }

        // `running`: the item is up, so Start is disabled; otherwise Stop and Restart are.
        ftxui::Element render(const std::string &state_label, ftxui::Color state_color, bool running)
        {
            return render(state_label, state_color, !running, running);
        }

        // For groups (compose projects) that can be partly running: both may be enabled.
        ftxui::Element render(const std::string &state_label, ftxui::Color state_color, bool can_start, bool can_stop)
        {
            using namespace ftxui;
            using namespace theme;
            visible_ = true;
            return hbox({
                text(state_label + "  ") | color(state_color) | bold,
                button(kStart, Colors::running_fg(), can_start, start_),
                button(kStop, Colors::error_fg(), can_stop, stop_),
                button(kRestart, WarningColor(), can_stop, restart_),
            });
        }

        // Back sits apart, at the left end of the title bar.
        ftxui::Element render_back()
        {
            return button(kBack, theme::TextColor(), true, back_);
        }

        // Opens a shell in the container; enabled once one was found.
        ftxui::Element render_shell(bool enabled)
        {
            visible_ = true;
            shell_visible_ = true;
            return log_button(">_", "Shell", "Alt+t", theme::AccentColor(), enabled, shell_);
        }

        // `full`: logs fill the page, so the button shrinks them back.
        ftxui::Element render_logs_toggle(bool full)
        {
            visible_ = true;
            return log_button(full ? "⤡" : "⤢", full ? "Shrink" : "Expand", "Enter", theme::AccentColor(), true,
                              logs_toggle_);
        }

        // `shown`: graphs are on. `side_by_side`: they sit beside the summary;
        // otherwise they replace it (narrow terminal).
        ftxui::Element render_graphs_toggle(bool shown, bool side_by_side)
        {
            visible_ = true;
            return log_button(graphs_glyph(shown, side_by_side), graphs_word(shown, side_by_side), "Alt+g",
                              theme::AccentColor(), true, graphs_toggle_);
        }

        // Call before the log-header buttons are drawn each frame: drops their key
        // hints, then their words, when the full set would leave the log title
        // less than kLogTitleRoom cells.
        void fit_log_header(bool with_shell, bool logs_full, bool graphs_shown, bool side_by_side)
        {
            const int room = ftxui::Terminal::Size().dimx - 2;
            for (log_level_ = 0; log_level_ < 2; ++log_level_)
            {
                int need = kLogTitleRoom + log_button_width("⤢", logs_full ? "Shrink" : "Expand", "Enter");
                if (!logs_full)
                    need += log_button_width(graphs_glyph(graphs_shown, side_by_side),
                                             graphs_word(graphs_shown, side_by_side), "Alt+g");
                if (with_shell)
                    need += log_button_width(">_", "Shell", "Alt+t");
                if (need <= room)
                    return;
            }
        }

        // The buttons of the log header: graphs toggle (not on full-size logs) and expand/shrink.
        ftxui::Element render_log_header(bool logs_full, bool graphs_shown, bool side_by_side)
        {
            if (logs_full)
                return render_logs_toggle(true);
            return ftxui::hbox({render_graphs_toggle(graphs_shown, side_by_side), render_logs_toggle(false)});
        }

        // Call at the start of every frame; render() marks the bar visible again.
        void hide()
        {
            visible_ = false;
            shell_visible_ = false; // only the container page draws it
        }

        Action hit(int x, int y) const
        {
            if (!visible_)
                return Action::None;
            if (start_.Contain(x, y))
                return Action::Start;
            if (stop_.Contain(x, y))
                return Action::Stop;
            if (restart_.Contain(x, y))
                return Action::Restart;
            if (back_.Contain(x, y))
                return Action::Back;
            if (logs_toggle_.Contain(x, y))
                return Action::ToggleLogs;
            if (graphs_toggle_.Contain(x, y))
                return Action::ToggleGraphs;
            if (shell_visible_ && shell_.Contain(x, y))
                return Action::Shell;
            return Action::None;
        }

    private:
        // Title-bar buttons: glyph, word, key.
        struct TitleButton
        {
            const char *glyph;
            const char *word;
            const char *key;
        };
        static constexpr TitleButton kBack{"←", "Back", "Esc"};
        static constexpr TitleButton kStart{"▶", "Start", "Alt+o"};
        static constexpr TitleButton kStop{"■", "Stop", "Alt+x"};
        static constexpr TitleButton kRestart{"↻", "Restart", "Alt+r"};
        // The name keeps this many cells before the buttons give up their words.
        static constexpr int kNameRoom = 24;
        int level_ = 0; // 0 full, 1 no key, 2 glyph only

        std::string label(const TitleButton &b) const
        {
            return level_ >= 2 ? std::string(b.glyph) : std::string(b.glyph) + " " + b.word;
        }
        std::string key(const TitleButton &b) const { return level_ >= 1 ? "" : b.key; }
        int button_width(const TitleButton &b) const
        {
            const auto k = key(b);
            return ftxui::string_width(" " + label(b) + " ") + (k.empty() ? 0 : ftxui::string_width(k + " ")) + 1;
        }
        ftxui::Element button(const TitleButton &b, ftxui::Color fg, bool enabled, ftxui::Box &box) const
        {
            return button(label(b), key(b), fg, enabled, box);
        }

        // " LOGS " + a name + "following  ", before the log buttons shorten.
        static constexpr int kLogTitleRoom = 40;
        int log_level_ = 0; // 0 full, 1 no key, 2 glyph only

        static const char *graphs_glyph(bool shown, bool side_by_side)
        {
            return !side_by_side && shown ? "▤" : "▦";
        }
        static const char *graphs_word(bool shown, bool side_by_side)
        {
            return side_by_side ? (shown ? "Hide graphs" : "Show graphs") : (shown ? "Info" : "Graphs");
        }
        int log_button_width(const std::string &glyph, const std::string &word, const std::string &key) const
        {
            const std::string label = log_level_ >= 2 ? glyph : glyph + " " + word;
            return ftxui::string_width(" " + label + " ") + (log_level_ >= 1 ? 0 : ftxui::string_width(key + " ")) + 1;
        }
        ftxui::Element log_button(const std::string &glyph, const std::string &word, const std::string &key,
                                  ftxui::Color fg, bool enabled, ftxui::Box &box) const
        {
            return button(log_level_ >= 2 ? glyph : glyph + " " + word, log_level_ >= 1 ? "" : key, fg, enabled, box);
        }

        bool visible_ = false;
        bool shell_visible_ = false;
        ftxui::Box start_;
        ftxui::Box stop_;
        ftxui::Box restart_;
        ftxui::Box back_;
        ftxui::Box logs_toggle_;
        ftxui::Box graphs_toggle_;
        ftxui::Box shell_;

        static ftxui::Element button(const std::string &label, const std::string &key, ftxui::Color fg, bool enabled,
                                     ftxui::Box &box)
        {
            using namespace ftxui;
            auto element = hbox({text(" " + label + " "), key.empty() ? text("") : text(key + " ") | dim}) |
                           bgcolor(theme::BorderColor());
            element = enabled ? element | color(fg) : element | color(theme::MutedColor()) | dim;
            return hbox({element | reflect(box), text(" ")});
        }
    };

} // namespace gsystemctl::ui
