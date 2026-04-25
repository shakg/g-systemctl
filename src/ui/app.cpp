#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ftxui/component/component_options.hpp>
#include <mutex>

using namespace ftxui;

namespace gsystemctl::ui
{
    namespace
    {
        Color BackgroundColor() { return Color::RGB(11, 12, 19); }
        Color PanelColor() { return Color::RGB(16, 17, 27); }
        Color BorderColor() { return Color::RGB(40, 43, 61); }
        Color MutedColor() { return Color::RGB(103, 110, 140); }
        Color TextColor() { return Color::RGB(180, 186, 208); }
        Color HeaderColor() { return Color::RGB(112, 119, 150); }
        Color AccentColor() { return Color::RGB(0, 220, 235); }
        Color WarningColor() { return Color::RGB(255, 190, 45); }
        Element cell(const std::string &value, int width, Color fg = TextColor(), bool bold_text = false)
        {
            auto element = text(value) | color(fg);
            if (bold_text)
            {
                element = element | bold;
            }
            return element | size(WIDTH, EQUAL, width);
        }

        Element right_cell(const std::string &value, int width, Color fg = TextColor())
        {
            return text(value) | color(fg) | align_right | size(WIDTH, EQUAL, width);
        }

        Element service_cell(const std::string &value, Color fg = TextColor(), bool bold_text = false)
        {
            auto element = text(value) | color(fg);
            if (bold_text)
            {
                element = element | bold;
            }
            return element | flex;
        }

        std::string shell_quote(const std::string &value)
        {
            std::string quoted = "'";
            for (char ch : value)
            {
                if (ch == '\'')
                {
                    quoted += "'\\''";
                }
                else
                {
                    quoted += ch;
                }
            }
            quoted += "'";
            return quoted;
        }
    }

    App::App(bool system_mode, const std::string &initial_filter) : screen_(ScreenInteractive::Fullscreen()), system_mode_(system_mode)
    {
        filter_text_ = initial_filter;
        executor_ = std::make_shared<SystemCommandExecutor>();
        service_manager_ = ServiceManager::create(executor_, system_mode_);
        logging_manager_ = LoggingManager::create(executor_);
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
        if (!executor_->can_execute_privileged_without_password())
        {
            open_auth_dialog(AuthAction::Toggle, service);
            return;
        }

        toggle_service(service);
    }

    void App::toggle_service(const ServiceUnit& service, const std::string& password)
    {
        status_message_ = "Toggling " + service.unit + "...";
        error_message_.clear();
        screen_.PostEvent(Event::Custom);

        auto [success, message] = service_manager_->toggle_service(service, password);
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

    void App::restart_selected_service()
    {
        if (filtered_services_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return;
        }

        const auto &service = filtered_services_[selected_index_];
        if (!executor_->can_execute_privileged_without_password())
        {
            open_auth_dialog(AuthAction::Restart, service);
            return;
        }

        restart_service(service);
    }

    void App::restart_service(const ServiceUnit& service, const std::string& password)
    {
        status_message_ = "Restarting " + service.unit + "...";
        error_message_.clear();
        screen_.PostEvent(Event::Custom);

        auto [success, message] = service_manager_->restart_service(service.unit, password);
        if (success)
        {
            status_message_ = "Successfully restarted " + service.unit;
            refresh_services();
        }
        else
        {
            error_message_ = "Failed to restart " + service.unit + ": " + message;
        }
    }

    void App::open_auth_dialog(AuthAction action, const ServiceUnit& service)
    {
        pending_auth_action_ = action;
        pending_auth_service_ = service;
        auth_password_.clear();
        auth_dialog_message_ = (action == AuthAction::Restart ? "Restart " : "Toggle ") + service.unit;
        auth_dialog_open_ = true;
        status_message_.clear();
        error_message_.clear();
        screen_.PostEvent(Event::Custom);
    }

    void App::submit_auth_dialog()
    {
        auto action = pending_auth_action_;
        auto service = pending_auth_service_;
        auto password = auth_password_;
        close_auth_dialog();

        if (password.empty())
        {
            error_message_ = "Sudo password is required";
            return;
        }

        if (action == AuthAction::Toggle)
        {
            toggle_service(service, password);
        }
        else if (action == AuthAction::Restart)
        {
            restart_service(service, password);
        }
    }

    void App::close_auth_dialog()
    {
        auth_dialog_open_ = false;
        auth_password_.clear();
        auth_dialog_message_.clear();
        pending_auth_action_ = AuthAction::None;
        pending_auth_service_ = ServiceUnit{};
        screen_.PostEvent(Event::Custom);
    }

    void App::edit_selected_service_file()
    {
        if (filtered_services_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return;
        }

        const auto &service = filtered_services_[selected_index_];
        if (detect_platform() == Platform::MacOS)
        {
            error_message_ = "Edit service file is not implemented on macOS";
            status_message_.clear();
            return;
        }

        std::string command = (system_mode_ ? "systemctl edit " : "systemctl --user edit ") + shell_quote(service.unit);
        status_message_ = "Editing " + service.unit + "...";
        error_message_.clear();
        screen_.PostEvent(Event::Custom);

        int exit_code = 0;
        screen_.WithRestoredIO([&] {
            exit_code = std::system(command.c_str());
        });

        if (exit_code == 0)
        {
            status_message_ = "Edited " + service.unit;
            refresh_services();
        }
        else
        {
            error_message_ = "Failed to edit " + service.unit;
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

        auto main_component = CatchEvent(renderer, [this, input](Event event)
                          {
        if (auth_dialog_open_) {
            return false;
        }
        if (event == Event::Special("\x1b" "q") || event == Event::Escape) {
            if (show_help_) {
                show_help_ = false;
                return true;
            }
            screen_.Exit();
            return true;
        }
        if (event == Event::Character('?')) {
            show_help_ = !show_help_;
            return true;
        }
        if (event == Event::Special("\x1b" "r")) {
            restart_selected_service();
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
        if (event == Event::Special("\x1b" "e")) {
            edit_selected_service_file();
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
                if (shortcuts_box_.Contain(mouse.x, mouse.y)) {
                    show_help_ = true;
                    return true;
                }
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

        InputOption password_option;
        password_option.password = true;
        auto password_input = Input(&auth_password_, "sudo password", password_option);
        auto auth_renderer = Renderer(password_input, [this, password_input] {
            return vbox({
                       text("Authentication required") | bold | color(AccentColor()),
                       separator() | color(BorderColor()),
                       text(auth_dialog_message_) | color(TextColor()),
                       hbox({
                           text("Password: ") | color(MutedColor()),
                           password_input->Render() | flex,
                       }),
                       separator() | color(BorderColor()),
                       text("Enter submit  Esc cancel") | color(MutedColor()),
                   }) |
                   size(WIDTH, GREATER_THAN, 44) |
                   borderStyled(ROUNDED, BorderColor()) |
                   bgcolor(PanelColor());
        });
        auto auth_modal = CatchEvent(auth_renderer, [this](Event event) {
            if (event == Event::Return) {
                submit_auth_dialog();
                return true;
            }
            if (event == Event::Escape) {
                close_auth_dialog();
                return true;
            }
            return false;
        });

        main_component |= Modal(auth_modal, &auth_dialog_open_);
        return main_component;
    }

    Element App::render()
    {
        bool show_status = !status_message_.empty() || !error_message_.empty();

        Elements layout;
        layout.push_back(render_top_bar());
        layout.push_back(separator() | color(BorderColor()));
        if (log_panel_open_)
        {
            layout.push_back(render_service_list() | flex);
            layout.push_back(separator() | color(BorderColor()));
            layout.push_back(render_log_panel() | size(HEIGHT, GREATER_THAN, 8) | flex);
        }
        else
        {
            layout.push_back(render_service_list() | flex);
        }
        if (show_status)
        {
            layout.push_back(separator() | color(BorderColor()));
            layout.push_back(render_status_bar());
        }
        layout.push_back(separator() | color(BorderColor()));
        layout.push_back(hbox({
            text(" Filter: ") | color(MutedColor()),
            text(filter_text_.empty() ? "█" : filter_text_ + "█") | color(AccentColor()),
            filler(),
            text(std::to_string(filtered_services_.size()) + "/" + std::to_string(services_.size()) + " services") | color(MutedColor()),
            text("  ·  ↑↓ navigate  ·  type to filter  ") | color(MutedColor()),
        }));

        auto page = vbox(std::move(layout)) | borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor());
        auto dialog_open = show_help_ || auth_dialog_open_;
        if (dialog_open)
        {
            page = page | color(Color::RGB(55, 59, 78)) | bgcolor(Color::RGB(4, 5, 9)) | dim;
        }

        if (show_help_)
        {
            return dbox({
                page,
                render_shortcuts_dialog() | clear_under | center,
            });
        }

        return page;
    }

    Element App::render_top_bar()
    {
        return hbox({
                   text(" g-systemctl ") | color(AccentColor()) | bold,
                   filler(),
                   text(" Shortcuts(?) ") | color(MutedColor()) | reflect(shortcuts_box_),
               }) |
               bgcolor(PanelColor());
    }

    Element App::render_shortcuts_dialog()
    {
        return render_help();
    }

    Element App::render_service_list()
    {
        if (filtered_services_.empty())
        {
            return text("No services found") | center | dim;
        }

        auto header = hbox({
                          text(" "),
                          service_cell("SERVICE", HeaderColor()),
                          cell("STATUS", 11, HeaderColor()),
                          right_cell("PID", 7, HeaderColor()),
                          right_cell("CPU", 7, HeaderColor()),
                          right_cell("MEM", 8, HeaderColor()),
                      }) |
                      bgcolor(PanelColor());

        Elements items;
        item_boxes_.resize(filtered_services_.size());
        toggle_button_boxes_.resize(filtered_services_.size());
        log_button_boxes_.resize(filtered_services_.size());

        for (size_t i = 0; i < filtered_services_.size(); ++i)
        {
            const auto &svc = filtered_services_[i];
            bool selected = (static_cast<int>(i) == selected_index_);
            bool active = svc.active == "active" || svc.is_running();
            auto status_color = active ? Colors::running_fg() : Colors::stopped_fg();
            std::string status_text = active ? "● active" : "○ inactive";

            auto card = hbox({
                service_cell(svc.unit, selected ? Color::White : TextColor(), selected),
                cell(status_text, 11, status_color, active),
                right_cell(svc.pid.empty() ? "-" : svc.pid, 7, MutedColor()),
                right_cell(svc.cpu.empty() ? "-" : svc.cpu, 7, MutedColor()),
                right_cell(svc.memory.empty() ? "-" : svc.memory, 8, MutedColor()),
            });
            if (selected)
            {
                card = hbox({
                           text("▌") | color(AccentColor()),
                           card | flex,
                       }) |
                       bgcolor(Colors::selected_bg()) | focus;
            }
            else
            {
                card = hbox({
                    text(" "),
                    card | flex,
                });
            }
            card = card | reflect(item_boxes_[i]);
            items.push_back(card);
        }

        return vbox({
            header,
            vbox(items) | vscroll_indicator | yframe | flex,
        }) | flex;
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

        return vbox({
                   hbox({
                       text(" LOGS ") | color(HeaderColor()),
                       text(unit) | color(WarningColor()),
                       filler(),
                       text("Alt+c close ") | color(MutedColor()),
                   }),
                   separator() | color(BorderColor()),
                   vbox(std::move(rendered_lines)) | color(TextColor()) | vscroll_indicator | yframe | flex,
               }) |
               bgcolor(PanelColor());
    }

    Element App::render_status_bar()
    {
        if (!error_message_.empty())
        {
            return text(" " + error_message_) | color(Colors::error_fg());
        }
        return text(" " + status_message_) | color(MutedColor());
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
                   text("  Alt+r        - Restart selected service"),
                   text("  ?            - Toggle this help screen"),
                   text("  Alt+l        - Stream logs for selected unit"),
                   text("  Alt+c        - Close log stream"),
                   text("  Alt+e        - Edit selected service file"),
                   text("  Alt+q / Esc  - Quit"),
                   text(""),
                   text("Filtering:") | bold,
                   text("  Type         - Filter services by name"),
                   text("  Backspace    - Delete last character"),
                   text(""),
                   separator(),
                   text("Press ? to close") | dim | center,
               }) |
               borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor()) | center;
    }

} // namespace gsystemctl::ui
