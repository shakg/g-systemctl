#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include <algorithm>
#include <cctype>

using namespace ftxui;

namespace gsystemctl::ui
{

    App::App(bool system_mode, const std::string &initial_filter) : screen_(ScreenInteractive::Fullscreen()), system_mode_(system_mode)
    {
        filter_text_ = initial_filter;
        auto executor = std::make_shared<SystemCommandExecutor>();
        service_manager_ = ServiceManager::create(executor, system_mode_);
        logging_manager_ = LoggingManager::create(executor);
        refresh_services();
    }

    int App::run()
    {
        auto component = create_main_component();
        screen_.Loop(component);
        return 0;
    }

    void App::refresh_services()
    {
        try
        {
            services_ = service_manager_->list_services();
            error_message_.clear();
            apply_filter();
        }
        catch (const std::exception &e)
        {
            error_message_ = std::string("Error: ") + e.what();
            services_.clear();
            filtered_services_.clear();
        }
    }

    void App::apply_filter()
    {
        if (filter_text_.empty())
        {
            filtered_services_ = services_;
        }
        else
        {
            filtered_services_.clear();
            std::string lower_filter = filter_text_;
            std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(),
                           [](unsigned char c)
                           { return std::tolower(c); });

            for (const auto &svc : services_)
            {
                std::string lower_unit = svc.unit;
                std::transform(lower_unit.begin(), lower_unit.end(), lower_unit.begin(),
                               [](unsigned char c)
                               { return std::tolower(c); });
                if (lower_unit.find(lower_filter) != std::string::npos)
                {
                    filtered_services_.push_back(svc);
                }
            }
        }

        if (selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            selected_index_ = std::max(0, static_cast<int>(filtered_services_.size()) - 1);
        }
    }

    void App::toggle_selected_service()
    {
        if (filtered_services_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return;
        }

        const auto &service = filtered_services_[selected_index_];
        status_message_ = "Toggling " + service.unit + "...";
        screen_.PostEvent(Event::Custom);

        auto [success, message] = service_manager_->toggle_service(service);
        if (success)
        {
            status_message_ = "Successfully toggled " + service.unit;
            refresh_services();
        }
        else
        {
            error_message_ = "Failed to toggle " + service.unit + ": " + message;
        }
    }

    void App::open_logs_for_selected_service()
    {
        if (filtered_services_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return;
        }

        const auto &service = filtered_services_[selected_index_];
        status_message_ = "Opening logs for " + service.unit + "...";
        screen_.PostEvent(Event::Custom);

        if (logging_manager_)
        {
            auto [success, message] = logging_manager_->open_logs(service.unit);
            if (!success)
            {
                error_message_ = "Unable to open logs: " + message;
            }
        }
        else
        {
            error_message_ = "No logging manager available";
        }
    }

    Component App::create_main_component()
    {
        auto input = Input(&filter_text_, "Filter services...");

        auto renderer = Renderer(input, [this, input]
                                 { return render(); });

        return CatchEvent(renderer, [this, input](Event event)
                          {
        if (event == Event::Special("\x1bq") || event == Event::Escape) {
            screen_.Exit();
            return true;
        }
        if (event == Event::Character('?')) {
            show_help_ = !show_help_;
            return true;
        }
        if (event == Event::Special("\x1br")) {
            refresh_services();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Special("\x1bk")) {
            if (selected_index_ > 0) {
                selected_index_--;
            }
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Special("\x1bj")) {
            if (selected_index_ < static_cast<int>(filtered_services_.size()) - 1) {
                selected_index_++;
            }
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        if (event == Event::Return) {
            toggle_selected_service();
            return true;
        }
        if (event == Event::Special("\x1bl")) {
            open_logs_for_selected_service();
            return true;
        }
        if (event.is_character()) {
            filter_text_ += event.character();
            apply_filter();
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        if (event == Event::Backspace && !filter_text_.empty()) {
            filter_text_.pop_back();
            apply_filter();
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp) {
                if (selected_index_ > 0) {
                    selected_index_--;
                }
                status_message_.clear();
                error_message_.clear();
                return true;
            }
            if (mouse.button == Mouse::WheelDown) {
                if (selected_index_ < static_cast<int>(filtered_services_.size()) - 1) {
                    selected_index_++;
                }
                status_message_.clear();
                error_message_.clear();
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                for (size_t i = 0; i < toggle_button_boxes_.size(); ++i) {
                    if (toggle_button_boxes_[i].Contain(mouse.x, mouse.y)) {
                        selected_index_ = static_cast<int>(i);
                        toggle_selected_service();
                        return true;
                    }
                }
                for (size_t i = 0; i < log_button_boxes_.size(); ++i) {
                    if (log_button_boxes_[i].Contain(mouse.x, mouse.y)) {
                        selected_index_ = static_cast<int>(i);
                        open_logs_for_selected_service();
                        return true;
                    }
                }
                for (size_t i = 0; i < item_boxes_.size(); ++i) {
                    if (item_boxes_[i].Contain(mouse.x, mouse.y)) {
                        selected_index_ = static_cast<int>(i);
                        status_message_.clear();
                        error_message_.clear();
                        return true;
                    }
                }
            }
        }
        return false; });
    }

    Element App::render()
    {
        if (show_help_)
        {
            return render_help();
        }

        auto filter_line = hbox({
            text("Filter: ") | dim,
            text(filter_text_.empty() ? "(type to filter)" : filter_text_) |
                (filter_text_.empty() ? dim : nothing),
            filler(),
            text(system_mode_ ? "[SYSTEM]" : "[USER]") | dim,
        });

        auto title = text(" g-systemctl ") | bold | color(Color::Cyan) | align_right;

        bool show_status = !status_message_.empty() || !error_message_.empty();

        Elements layout;
        layout.push_back(filter_line);
        layout.push_back(separator());
        layout.push_back(render_service_list() | flex);
        if (show_status)
        {
            layout.push_back(separator());
            layout.push_back(render_status_bar());
        }

        return window(title, vbox(std::move(layout)));
    }

    Element App::render_service_list()
    {
        if (filtered_services_.empty())
        {
            return text("No services found") | center | dim;
        }

        Elements items;
        item_boxes_.resize(filtered_services_.size());
        toggle_button_boxes_.resize(filtered_services_.size());
        log_button_boxes_.resize(filtered_services_.size());
        for (size_t i = 0; i < filtered_services_.size(); ++i)
        {
            const auto &svc = filtered_services_[i];
            bool selected = (static_cast<int>(i) == selected_index_);
            auto card = service_card(svc.unit, svc.sub, svc.description,
                                     svc.is_running(), selected,
                                     toggle_button_boxes_[i], log_button_boxes_[i]);
            if (selected)
            {
                card = card | focus;
            }
            card = card | reflect(item_boxes_[i]);
            items.push_back(card);
        }

        return vbox(items) | vscroll_indicator | frame | flex;
    }

    Element App::render_status_bar()
    {
        if (!error_message_.empty())
        {
            return text(error_message_) | color(Colors::error_fg());
        }
        return text(status_message_) | dim;
    }

    Element App::render_help()
    {
        return vbox({
                   text("g-systemctl - Help") | bold | center,
                   separator(),
                   text(""),
                   text("Navigation:") | bold,
                   text("  Up / Alt+k   - Move selection up"),
                   text("  Down / Alt+j - Move selection down"),
                   text("  Enter        - Toggle selected service (start/stop)"),
                   text(""),
                   text("Actions:") | bold,
                   text("  Alt+r        - Refresh service list"),
                   text("  ?            - Toggle this help screen"),
                   text("  Alt+l        - Open logs for selected unit (tmux only)"),
                   text("  Alt+q / Esc  - Quit"),
                   text(""),
                   text("Filtering:") | bold,
                   text("  Type         - Filter services by name"),
                   text("  Backspace    - Delete last character"),
                   text(""),
                   separator(),
                   text("Press ? to close") | dim | center,
               }) |
               border | center;
    }

} // namespace gsystemctl::ui
