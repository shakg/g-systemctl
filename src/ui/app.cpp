#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include <algorithm>
#include <cctype>
#include <mutex>

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
        status_message_ = "Streaming logs for " + service.unit + "...";
        error_message_.clear();
        screen_.PostEvent(Event::Custom);

        if (logging_manager_)
        {
            log_stream_.reset();
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                log_unit_ = service.unit;
                log_lines_.clear();
                log_lines_.push_back(system_mode_
                                         ? "Starting: journalctl -u " + service.unit + " -f -n 100"
                                         : "Starting: journalctl --user-unit " + service.unit + " -f -n 100");
                log_panel_open_ = true;
            }

            screen_.PostEvent(Event::Custom);

            log_stream_ = logging_manager_->stream_logs(service.unit, system_mode_, [this](std::string line) {
                {
                    std::lock_guard<std::mutex> lock(log_mutex_);
                    log_lines_.push_back(std::move(line));
                    if (log_lines_.size() > 500)
                    {
                        log_lines_.erase(log_lines_.begin(), log_lines_.begin() + 100);
                    }
                }
                screen_.PostEvent(Event::Custom);
            });

            if (!log_stream_)
            {
                error_message_ = "Unable to start log stream";
                std::lock_guard<std::mutex> lock(log_mutex_);
                log_lines_.push_back(error_message_);
            }
            else
            {
                status_message_.clear();
            }
        }
        else
        {
            error_message_ = "No logging manager available";
        }
    }

    void App::close_logs()
    {
        log_stream_.reset();
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_panel_open_ = false;
        log_unit_.clear();
        log_lines_.clear();
    }

    Component App::create_main_component()
    {
        auto input = Input(&filter_text_, "Filter services...");

        auto renderer = Renderer(input, [this, input]
                                 { return render(); });

        return CatchEvent(renderer, [this, input](Event event)
                          {
        if (event == Event::Special("\x1b" "q") || event == Event::Escape) {
            screen_.Exit();
            return true;
        }
        if (event == Event::Character('?')) {
            show_help_ = !show_help_;
            return true;
        }
        if (event == Event::Special("\x1b" "r")) {
            refresh_services();
            return true;
        }
        if (event == Event::ArrowUp || event == Event::Special("\x1b" "k")) {
            if (selected_index_ > 0) {
                selected_index_--;
            }
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Special("\x1b" "j")) {
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
        if (event == Event::Special("\x1b" "l")) {
            open_logs_for_selected_service();
            return true;
        }
        if (event == Event::Special("\x1b" "c")) {
            close_logs();
            status_message_.clear();
            error_message_.clear();
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
        if (log_panel_open_)
        {
            layout.push_back(render_service_list() | flex);
            layout.push_back(separator());
            layout.push_back(render_log_panel() | size(HEIGHT, GREATER_THAN, 8) | flex);
        }
        else
        {
            layout.push_back(render_service_list() | flex);
        }
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

    Element App::render_log_panel()
    {
        std::string unit;
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            unit = log_unit_;
            lines = log_lines_;
        }

        Elements rendered_lines;
        for (const auto &line : lines)
        {
            rendered_lines.push_back(text(line));
        }

        if (rendered_lines.empty())
        {
            rendered_lines.push_back(text("Waiting for log output...") | dim);
        }

        return window(text(" logs: " + unit + "  Alt+c close ") | bold,
                      vbox(std::move(rendered_lines)) | vscroll_indicator | yframe | flex);
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
                   text("  Alt+l        - Stream logs for selected unit"),
                   text("  Alt+c        - Close log stream"),
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
