#pragma once

#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>

namespace gsystemctl::ui
{

    // Clickable buttons on the detail page: Start / Stop / Restart / Back in the
    // title bar and Expand / Shrink in the log header. Each has a keyboard shortcut.
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
        };

        // `running`: the item is up, so Start is disabled; otherwise Stop and Restart are.
        ftxui::Element render(const std::string &state_label, ftxui::Color state_color, bool running)
        {
            using namespace ftxui;
            using namespace theme;
            visible_ = true;
            return hbox({
                text(state_label + "  ") | color(state_color) | bold,
                button("▶ Start", "Alt+o", Colors::running_fg(), !running, start_),
                button("■ Stop", "Alt+x", Colors::error_fg(), running, stop_),
                button("↻ Restart", "Alt+r", WarningColor(), running, restart_),
                button("← Back", "Esc", TextColor(), true, back_),
            });
        }

        // `full`: logs fill the page, so the button shrinks them back.
        ftxui::Element render_logs_toggle(bool full)
        {
            visible_ = true;
            return button(full ? "⤡ Shrink" : "⤢ Expand", "Enter", theme::AccentColor(), true, logs_toggle_);
        }

        // `shown`: graphs are on. `side_by_side`: they sit beside the summary;
        // otherwise they replace it (narrow terminal).
        ftxui::Element render_graphs_toggle(bool shown, bool side_by_side)
        {
            visible_ = true;
            const char *label = side_by_side ? (shown ? "▦ Hide graphs" : "▦ Show graphs") : (shown ? "▤ Info" : "▦ Graphs");
            return button(label, "Alt+g", theme::AccentColor(), true, graphs_toggle_);
        }

        // The buttons of the log header: graphs toggle (not on full-size logs) and expand/shrink.
        ftxui::Element render_log_header(bool logs_full, bool graphs_shown, bool side_by_side)
        {
            if (logs_full)
                return render_logs_toggle(true);
            return ftxui::hbox({render_graphs_toggle(graphs_shown, side_by_side), render_logs_toggle(false)});
        }

        // Call at the start of every frame; render() marks the bar visible again.
        void hide() { visible_ = false; }

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
            return Action::None;
        }

    private:
        bool visible_ = false;
        ftxui::Box start_;
        ftxui::Box stop_;
        ftxui::Box restart_;
        ftxui::Box back_;
        ftxui::Box logs_toggle_;
        ftxui::Box graphs_toggle_;

        static ftxui::Element button(const std::string &label, const std::string &key, ftxui::Color fg, bool enabled,
                                     ftxui::Box &box)
        {
            using namespace ftxui;
            auto element = hbox({text(" " + label + " "), text(key + " ") | dim}) | bgcolor(theme::BorderColor());
            element = enabled ? element | color(fg) : element | color(theme::MutedColor()) | dim;
            return hbox({element | reflect(box), text(" ")});
        }
    };

} // namespace gsystemctl::ui
