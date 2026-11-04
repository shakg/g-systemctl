#pragma once

#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <string>

namespace gsystemctl::ui
{

    // Clickable Start / Stop / Restart / Close buttons shown in the header of
    // the logs and details panels. The same actions have keyboard shortcuts.
    class ActionBar
    {
    public:
        enum class Action
        {
            None,
            Start,
            Stop,
            Restart,
            Close,
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
                button("✕ Close", "Esc", TextColor(), true, close_),
            });
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
            if (close_.Contain(x, y))
                return Action::Close;
            return Action::None;
        }

    private:
        bool visible_ = false;
        ftxui::Box start_;
        ftxui::Box stop_;
        ftxui::Box restart_;
        ftxui::Box close_;

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
