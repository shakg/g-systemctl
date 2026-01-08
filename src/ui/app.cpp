#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include <algorithm>
#include <cctype>

using namespace ftxui;

namespace gsystemctl::ui {

App::App(bool system_mode) : screen_(ScreenInteractive::Fullscreen()), system_mode_(system_mode) {
    auto executor = std::make_shared<SystemCommandExecutor>();
    service_manager_ = ServiceManager::create(executor, system_mode_);
    refresh_services();
}

int App::run() {
    auto component = create_main_component();
    screen_.Loop(component);
    return 0;
}

void App::refresh_services() {
    try {
        services_ = service_manager_->list_services();
        error_message_.clear();
        status_message_ = "Loaded " + std::to_string(services_.size()) + " services";
        apply_filter();
    } catch (const std::exception& e) {
        error_message_ = std::string("Error: ") + e.what();
        services_.clear();
        filtered_services_.clear();
    }
}

void App::apply_filter() {
    if (filter_text_.empty()) {
        filtered_services_ = services_;
    } else {
        filtered_services_.clear();
        std::string lower_filter = filter_text_;
        std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        for (const auto& svc : services_) {
            std::string lower_unit = svc.unit;
            std::transform(lower_unit.begin(), lower_unit.end(), lower_unit.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_unit.find(lower_filter) != std::string::npos) {
                filtered_services_.push_back(svc);
            }
        }
    }

    if (selected_index_ >= static_cast<int>(filtered_services_.size())) {
        selected_index_ = std::max(0, static_cast<int>(filtered_services_.size()) - 1);
    }
}

void App::toggle_selected_service() {
    if (filtered_services_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(filtered_services_.size())) {
        return;
    }

    const auto& service = filtered_services_[selected_index_];
    status_message_ = "Toggling " + service.unit + "...";
    screen_.PostEvent(Event::Custom);

    auto [success, message] = service_manager_->toggle_service(service);
    if (success) {
        status_message_ = "Successfully toggled " + service.unit;
        refresh_services();
    } else {
        error_message_ = "Failed to toggle " + service.unit + ": " + message;
    }
}

Component App::create_main_component() {
    auto input = Input(&filter_text_, "Filter services...");

    auto renderer = Renderer(input, [this, input] {
        return render();
    });

    return CatchEvent(renderer, [this, input](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            screen_.Exit();
            return true;
        }
        if (event == Event::Character('?')) {
            show_help_ = !show_help_;
            return true;
        }
        if (event == Event::Character('r')) {
            refresh_services();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
            if (selected_index_ > 0) {
                selected_index_--;
            }
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (selected_index_ < static_cast<int>(filtered_services_.size()) - 1) {
                selected_index_++;
            }
            return true;
        }
        if (event == Event::Return) {
            toggle_selected_service();
            return true;
        }
        if (event.is_character()) {
            filter_text_ += event.character();
            apply_filter();
            return true;
        }
        if (event == Event::Backspace && !filter_text_.empty()) {
            filter_text_.pop_back();
            apply_filter();
            return true;
        }
        return false;
    });
}

Element App::render() {
    if (show_help_) {
        return render_help();
    }

    auto header = hbox({
        text("g-systemctl") | bold | color(Color::Cyan),
        filler(),
        text(system_mode_ ? "[SYSTEM] " : "[USER] ") | dim,
        text("Press ? for help") | dim,
    });

    auto filter_box = hbox({
        text("Filter: ") | dim,
        text(filter_text_.empty() ? "(type to filter)" : filter_text_) |
            (filter_text_.empty() ? dim : nothing),
    }) | border;

    return vbox({
        header,
        separator(),
        filter_box,
        separator(),
        render_service_list() | flex,
        separator(),
        render_status_bar(),
    }) | border;
}

Element App::render_service_list() {
    if (filtered_services_.empty()) {
        return text("No services found") | center | dim;
    }

    Elements items;
    for (size_t i = 0; i < filtered_services_.size(); ++i) {
        const auto& svc = filtered_services_[i];
        bool selected = (static_cast<int>(i) == selected_index_);
        items.push_back(service_card(svc.unit, svc.sub, svc.description,
                                     svc.is_running(), selected));
    }

    return vbox(items) | vscroll_indicator | frame | flex;
}

Element App::render_status_bar() {
    if (!error_message_.empty()) {
        return text(error_message_) | color(Colors::error_fg());
    }
    return hbox({
        text(status_message_) | dim,
        filler(),
        text("q:quit r:refresh Enter:toggle") | dim,
    });
}

Element App::render_help() {
    return vbox({
        text("g-systemctl - Help") | bold | center,
        separator(),
        text(""),
        text("Navigation:") | bold,
        text("  Up/k      - Move selection up"),
        text("  Down/j    - Move selection down"),
        text("  Enter     - Toggle selected service (start/stop)"),
        text(""),
        text("Actions:") | bold,
        text("  r         - Refresh service list"),
        text("  ?         - Toggle this help screen"),
        text("  q/Esc     - Quit"),
        text(""),
        text("Filtering:") | bold,
        text("  Type      - Filter services by name"),
        text("  Backspace - Delete last character"),
        text(""),
        separator(),
        text("Press ? to close") | dim | center,
    }) | border | center;
}

} // namespace gsystemctl::ui
