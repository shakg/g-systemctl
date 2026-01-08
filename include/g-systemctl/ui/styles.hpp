#pragma once

#include <ftxui/dom/elements.hpp>

namespace gsystemctl::ui {

struct Colors {
    static ftxui::Color running_fg();
    static ftxui::Color running_bg();
    static ftxui::Color stopped_fg();
    static ftxui::Color stopped_bg();
    static ftxui::Color selected_bg();
    static ftxui::Color error_fg();
};

ftxui::Element service_card(const std::string& name, const std::string& status,
                            const std::string& description, bool is_running, bool selected);

} // namespace gsystemctl::ui
