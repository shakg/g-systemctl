#include "g-systemctl/ui/styles.hpp"

using namespace ftxui;

namespace gsystemctl::ui
{

    Color Colors::running_fg() { return Color::RGB(0, 220, 132); }
    Color Colors::running_bg() { return Color::RGB(0, 220, 132); }
    Color Colors::stopped_fg() { return Color::RGB(100, 107, 134); }
    Color Colors::stopped_bg() { return Color::RGB(68, 74, 98); }
    Color Colors::selected_bg() { return Color::RGB(27, 29, 37); }
    Color Colors::error_fg() { return Color::RGB(255, 80, 96); }

    Element service_card(const std::string &name, const std::string &status,
                         const std::string &description, bool is_running, bool selected,
                         Box &toggle_box, Box &log_box)
    {
        auto status_color = is_running ? Colors::running_fg() : Colors::stopped_fg();
        auto button_text = is_running ? "STOP" : "START";
        auto button_color = is_running ? Color::Red : Color::Green;

        auto status_element = text(status) | color(status_color);
        auto log_element = text(" [LOG] ") | color(Color::Cyan) | bold | reflect(log_box);
        auto toggle_element = text(" [" + std::string(button_text) + "] ") | color(button_color) | bold | reflect(toggle_box);

        auto content = vbox({
            hbox({
                text(name) | bold | flex,
                toggle_element,
                log_element,
            }),
            hbox({
                status_element,
                text(" · ") | dim,
                text(description) | dim | flex,
            }),
        });

        auto card = content | border;
        if (selected)
        {
            card = card | inverted;
        }

        return card;
    }

} // namespace gsystemctl::ui
