#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include "g-systemctl/container/container_manager.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ftxui/component/animation.hpp>
#include <ftxui/component/component_options.hpp>
#include <iostream>
#include <mutex>

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    App::App(bool system_mode, const std::string &initial_filter, Tab initial_tab)
        : screen_(ScreenInteractive::Fullscreen()), system_mode_(system_mode), service_logs_(screen_)
    {
        executor_ = std::make_shared<SystemCommandExecutor>();
        service_manager_ = ServiceManager::create(executor_, system_mode_);
        logging_manager_ = LoggingManager::create(executor_);
        containers_view_ = std::make_unique<ContainersView>(screen_, ContainerManager::create(executor_));
        if (initial_tab == Tab::Containers)
        {
            containers_view_->set_filter(initial_filter);
        }
        else
        {
            filter_text_ = initial_filter;
        }
        refresh_services();
        switch_tab(initial_tab);
    }

    App::~App()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
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

    void App::request_daemon_reload()
    {
        if (!service_manager_->supports_unit_files())
        {
            return;
        }
        if (system_mode_ && !executor_->can_execute_privileged_without_password())
        {
            open_auth_dialog(AuthAction::DaemonReload);
            return;
        }
        daemon_reload();
    }

    void App::daemon_reload(const std::string& password)
    {
        busy_ = true;
        busy_message_ = "Reloading systemd daemon...";
        status_message_.clear();
        error_message_.clear();

        if (worker_.joinable())
        {
            worker_.join();
        }
        worker_ = std::thread([this, password] {
            auto result = service_manager_->daemon_reload(password);
            screen_.Post([this, result] {
                busy_ = false;
                busy_message_.clear();
                if (result.first)
                {
                    refresh_services();
                    status_message_ = "Daemon reloaded successfully.";
                }
                else
                {
                    error_message_ = "Daemon reload failed: " + result.second;
                }
            });
            screen_.PostEvent(Event::Custom);
        });
    }

    void App::edit_selected_service()
    {
        if (filtered_services_.empty() || selected_index_ < 0 ||
            selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return;
        }

        const auto service = filtered_services_[selected_index_];
        if (!service_manager_->supports_unit_files())
        {
            error_message_ = "Editing service files is not supported on this platform";
            return;
        }

        // Hand the terminal to the editor, then take it back.
        std::pair<bool, std::string> result;
        screen_.WithRestoredIO([this, &service, &result] {
            result = service_manager_->edit_service(service.unit);
            if (!result.first)
            {
                // Keep the editor/systemctl error on screen until the user has read it.
                std::cout << "\n" << result.second << ". Press Enter to return to g-systemctl..." << std::flush;
                std::string line;
                std::getline(std::cin, line);
            }
        })();

        if (result.first)
        {
            refresh_services();
            status_message_ = "Edited " + service.unit + " (daemon reloaded)";
        }
        else
        {
            error_message_ = "Failed to edit " + service.unit + ": " + result.second;
        }
    }

    void App::open_auth_dialog(AuthAction action, const ServiceUnit& service)
    {
        pending_auth_action_ = action;
        pending_auth_service_ = service;
        auth_password_.clear();
        switch (action)
        {
        case AuthAction::Restart:
            auth_dialog_message_ = "Restart " + service.unit;
            break;
        case AuthAction::DaemonReload:
            auth_dialog_message_ = "Reload systemd daemon";
            break;
        default:
            auth_dialog_message_ = "Toggle " + service.unit;
            break;
        }
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
        else if (action == AuthAction::DaemonReload)
        {
            daemon_reload(password);
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
            service_logs_.open(service.unit, system_mode_
                                                 ? "Starting: journalctl -u " + service.unit + " -f -n 100"
                                                 : "Starting: journalctl --user-unit " + service.unit + " -f -n 100");
            screen_.PostEvent(Event::Custom);

            auto stream = logging_manager_->stream_logs(service.unit, system_mode_, service_logs_.sink());
            if (!stream)
            {
                error_message_ = "Unable to start log stream";
                service_logs_.append(error_message_);
            }
            else
            {
                status_message_.clear();
            }
            service_logs_.attach(std::move(stream));
        }
        else
        {
            error_message_ = "No logging manager available";
        }
    }

    void App::close_logs()
    {
        service_logs_.close();
    }

    void App::switch_tab(Tab tab)
    {
        active_tab_ = tab;
        containers_view_->set_active(tab == Tab::Containers);
        screen_.PostEvent(Event::Custom);
    }

    bool App::handle_services_event(Event event)
    {
        if (event == Event::Special("\x1b" "r")) {
            restart_selected_service();
            return true;
        }
        if (event == Event::Special("\x1b" "R")) {
            request_daemon_reload();
            return true;
        }
        if (event == Event::Special("\x1b" "e")) {
            edit_selected_service();
            return true;
        }
        if (service_logs_.is_open() && event == Event::PageUp) {
            service_logs_.scroll(-10);
            return true;
        }
        if (service_logs_.is_open() && event == Event::PageDown) {
            service_logs_.scroll(10);
            return true;
        }
        if (service_logs_.is_open() && (event == Event::ArrowUp || event == Event::Special("\x1b" "k"))) {
            service_logs_.scroll(-1);
            return true;
        }
        if (service_logs_.is_open() && (event == Event::ArrowDown || event == Event::Special("\x1b" "j"))) {
            service_logs_.scroll(1);
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
            if (service_logs_.is_open() && service_logs_.contains(mouse.x, mouse.y)) {
                if (mouse.button == Mouse::WheelUp) {
                    service_logs_.scroll(-3);
                    return true;
                }
                if (mouse.button == Mouse::WheelDown) {
                    service_logs_.scroll(3);
                    return true;
                }
            }
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
                if (reload_button_box_.Contain(mouse.x, mouse.y)) {
                    request_daemon_reload();
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
        return false;
    }

    Component App::create_main_component()
    {
        auto input = Input(&filter_text_, "Filter services...");

        auto renderer = Renderer(input, [this]
                                 { return render(); });

        auto main_component = CatchEvent(renderer, [this](Event event)
                          {
        if (busy_) {
            return true;
        }
        if (auth_dialog_open_ || containers_view_->edit_dialog_open()) {
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
        if (event == Event::Tab || event == Event::TabReverse) {
            switch_tab(active_tab_ == Tab::Services ? Tab::Containers : Tab::Services);
            return true;
        }
        if (event == Event::Special("\x1b" "1")) {
            switch_tab(Tab::Services);
            return true;
        }
        if (event == Event::Special("\x1b" "2")) {
            switch_tab(Tab::Containers);
            return true;
        }
        if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed) {
            const auto& mouse = event.mouse();
            if (services_tab_box_.Contain(mouse.x, mouse.y)) {
                switch_tab(Tab::Services);
                return true;
            }
            if (containers_tab_box_.Contain(mouse.x, mouse.y)) {
                switch_tab(Tab::Containers);
                return true;
            }
            if (shortcuts_box_.Contain(mouse.x, mouse.y)) {
                show_help_ = true;
                return true;
            }
            if (active_tab_ == Tab::Containers && reload_button_box_.Contain(mouse.x, mouse.y)) {
                containers_view_->refresh_now();
                return true;
            }
        }
        if (active_tab_ == Tab::Containers) {
            return containers_view_->handle_event(event);
        }
        return handle_services_event(event); });

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
        main_component |= Modal(containers_view_->edit_dialog(), containers_view_->edit_dialog_open_flag());
        return main_component;
    }

    Element App::render()
    {
        const bool containers = active_tab_ == Tab::Containers;
        const auto &status = containers ? containers_view_->status_message() : status_message_;
        const auto &error = containers ? containers_view_->error_message() : error_message_;

        Elements layout;
        layout.push_back(render_top_bar());
        layout.push_back(separator() | color(BorderColor()));
        if (containers)
        {
            layout.push_back(containers_view_->render_body() | flex);
        }
        else if (service_logs_.is_open())
        {
            layout.push_back(render_service_list() | size(HEIGHT, EQUAL, 2));
            layout.push_back(separator() | color(BorderColor()));
            layout.push_back(render_log_panel() | size(HEIGHT, GREATER_THAN, 8) | flex);
        }
        else
        {
            layout.push_back(render_service_list() | flex);
        }
        if (!status.empty() || !error.empty())
        {
            layout.push_back(separator() | color(BorderColor()));
            layout.push_back(!error.empty() ? text(" " + error) | color(Colors::error_fg())
                                            : text(" " + status) | color(MutedColor()));
        }
        layout.push_back(separator() | color(BorderColor()));

        const auto &filter = containers ? containers_view_->filter_text() : filter_text_;
        const auto summary = containers ? containers_view_->summary()
                                        : std::to_string(filtered_services_.size()) + "/" + std::to_string(services_.size()) + " services";
        layout.push_back(hbox({
            text(" Filter: ") | color(MutedColor()),
            text(filter + "█") | color(AccentColor()),
            filler(),
            text(summary) | color(MutedColor()),
            text("  ·  ↑↓ navigate  ·  type to filter  ·  Tab switch  ") | color(MutedColor()),
        }));

        auto page = vbox(std::move(layout)) | borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor());
        auto dialog_open = show_help_ || auth_dialog_open_ || containers_view_->edit_dialog_open();
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

    Element App::render_tabs()
    {
        auto tab = [this](const std::string &label, Tab tab, Box &box) {
            bool active = active_tab_ == tab;
            auto element = text(" " + label + " ");
            element = active ? element | color(Color::White) | bold | bgcolor(Colors::selected_bg())
                             : element | color(MutedColor());
            return element | reflect(box);
        };
        return hbox({
            tab("Services", Tab::Services, services_tab_box_),
            text("│") | color(BorderColor()),
            tab("Containers", Tab::Containers, containers_tab_box_),
        });
    }

    Element App::render_top_bar()
    {
        const bool containers = active_tab_ == Tab::Containers;
        const bool busy = containers ? containers_view_->busy() : busy_;
        const auto &busy_message = containers ? containers_view_->busy_message() : busy_message_;

        Element reload_button;
        if (busy)
        {
            // Braille spinner, advanced by wall clock so speed is independent of frame rate.
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            auto frame = static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count() / 80);
            reload_button = hbox({
                spinner(15, frame) | color(WarningColor()),
                text(" " + busy_message + " ") | color(WarningColor()),
            });
            animation::RequestAnimationFrame();
        }
        else if (containers)
        {
            reload_button = text(" Refresh(Alt+R) ") | color(MutedColor());
        }
        else if (service_manager_->supports_unit_files())
        {
            reload_button = text(" Reload(Alt+R) ") | color(MutedColor());
        }
        else
        {
            reload_button = text(" Reload (n/a on macOS) ") | color(MutedColor()) | dim;
        }

        return hbox({
                   text(" g-systemctl ") | color(AccentColor()) | bold,
                   text(" "),
                   render_tabs(),
                   filler(),
                   reload_button | reflect(reload_button_box_),
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
                          flex_cell("SERVICE", HeaderColor()),
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
                flex_cell(svc.unit, selected ? Color::White : TextColor(), selected),
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
        return service_logs_.render();
    }

    Element App::render_help()
    {
        auto services = vbox({
            text("Services") | bold,
            text("  Enter        Start / stop"),
            text("  Alt+r        Restart"),
            text("  Alt+e        Edit unit file ($GSYSTEMCTL_EDITOR)"),
            text("  Alt+Shift+r  systemctl daemon-reload"),
            text("  Alt+l        Stream logs"),
            text("  Alt+c        Close logs"),
        });
        auto containers = vbox({
            text("Containers") | bold,
            text("  Enter        Start / stop"),
            text("  Alt+r        Restart"),
            text("  Alt+l        Stream logs"),
            text("  Alt+i        Inspect (env, mounts, limits)"),
            text("  Alt+u        Edit resource limits"),
            text("  Alt+s        Show/hide secret env values"),
            text("  Alt+Shift+r  Refresh now"),
            text("  Alt+c        Close panel"),
        });
        return vbox({
                   text("g-systemctl - Help") | bold | center,
                   separator(),
                   text("General") | bold,
                   text("  Tab / Alt+1 / Alt+2   Switch tab"),
                   text("  Up/Down, Alt+k/j      Move selection (scroll when a panel is open)"),
                   text("  PgUp / PgDn           Scroll panel"),
                   text("  type / Backspace      Filter"),
                   text("  Alt+q / Esc           Quit"),
                   text(""),
                   hbox({services, text("    "), containers}),
                   text(""),
                   text("Press ? to close") | dim | center,
               }) |
               borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor()) | center;
    }

} // namespace gsystemctl::ui
