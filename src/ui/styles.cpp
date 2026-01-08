#include "g-systemctl/ui/styles.hpp"

using namespace ftxui;

namespace gsystemctl::ui {

Color Colors::running_fg() { return Color::Green; }
Color Colors::running_bg() { return Color::GreenLight; }
Color Colors::stopped_fg() { return Color::GrayDark; }
Color Colors::stopped_bg() { return Color::GrayLight; }
Color Colors::selected_bg() { return Color::Blue; }
Color Colors::error_fg() { return Color::Red; }

Element service_card(const std::string& name, const std::string& status,
                     const std::string& description, bool is_running, bool selected) {
    auto status_color = is_running ? Colors::running_fg() : Colors::stopped_fg();
    auto button_text = is_running ? "STOP" : "START";
    auto button_color = is_running ? Color::Red : Color::Green;

    auto status_element = text(status) | color(status_color);
    auto button_element = text(" [" + std::string(button_text) + "] ") | color(button_color) | bold;

    auto content = vbox({
        hbox({
            text(name) | bold | flex,
            button_element,
        }),
        hbox({
            text("Status: ") | dim,
            status_element,
        }),
        text(description) | dim,
    });

    auto card = content | border;
    if (selected) {
        card = card | inverted;
    }

    return card;
}

} // namespace gsystemctl::ui
