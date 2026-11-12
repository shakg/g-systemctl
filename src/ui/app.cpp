#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include "g-systemctl/container/container_manager.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <ftxui/screen/terminal.hpp>
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

    App::App(bool system_mode, const std::string &initial_filter, Tab initial_tab, bool fallback_to_services)
        : screen_(ScreenInteractive::Fullscreen()), system_mode_(system_mode), service_logs_(screen_)
    {
        executor_ = std::make_shared<SystemCommandExecutor>();
        service_manager_ = ServiceManager::create(executor_, system_mode_);
        logging_manager_ = LoggingManager::create(executor_);
        containers_view_ = std::make_unique<ContainersView>(screen_, ContainerManager::create(executor_));
        containers_view_->set_graph_toggle(&graphs_);
        if (fallback_to_services)
        {
            containers_view_->set_on_unavailable([this] {
                if (active_tab_ != Tab::Containers)
                    return;
                // Carry over a filter given with -f.
                if (filter_text_.empty() && !containers_view_->filter_text().empty())
                {
                    filter_text_ = containers_view_->filter_text();
                    containers_view_->set_filter("");
                    apply_filter();
                }
                switch_tab(Tab::Services);
            });
        }
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
        sampler_ = std::thread([this] { sampler_loop(); });
    }

    App::~App()
    {
        {
            std::lock_guard<std::mutex> lock(sampler_mutex_);
            sampler_stop_ = true;
        }
        sampler_cv_.notify_all();
        if (sampler_.joinable())
        {
            sampler_.join();
        }
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

    namespace
    {
        bool service_is_active(const ServiceUnit &service)
        {
            return service.active == "active" || service.is_running();
        }
    } // namespace

    const ServiceUnit *App::selected_service() const
    {
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_services_.size()))
        {
            return nullptr;
        }
        return &filtered_services_[selected_index_];
    }

    const ServiceUnit *App::action_target() const
    {
        if (!detail_open_)
        {
            return selected_service();
        }
        auto it = std::find_if(services_.begin(), services_.end(),
                               [this](const ServiceUnit &s) { return s.unit == detail_unit_; });
        return it == services_.end() ? nullptr : &*it;
    }

    void App::request_service_action(AuthAction action)
    {
        const auto *target = action_target();
        if (!target)
        {
            return;
        }
        const auto service = *target;
        const bool active = service_is_active(service);
        status_message_.clear();
        error_message_.clear();

        if (action == AuthAction::Start && active)
        {
            status_message_ = service.unit + " is already running";
            return;
        }
        if ((action == AuthAction::Stop || action == AuthAction::Restart) && !active)
        {
            status_message_ = service.unit + " is not running (Alt+o to start)";
            return;
        }

        // The password prompt is itself a deliberate step, so it replaces the confirmation.
        if (!executor_->can_execute_privileged_without_password())
        {
            open_auth_dialog(action, service);
            return;
        }
        if (action == AuthAction::Start)
        {
            run_service_action(action, service);
            return;
        }
        const std::string verb = action == AuthAction::Stop ? "Stop" : "Restart";
        confirm_.ask(verb + " service " + service.unit + "?", verb,
                     [this, action, service] { run_service_action(action, service); });
    }

    void App::run_service_action(AuthAction action, const ServiceUnit &service, const std::string &password)
    {
        const bool start = action == AuthAction::Start;
        const bool stop = action == AuthAction::Stop;
        const std::string unit = service.unit;
        const std::string busy = start ? "Starting " : stop ? "Stopping " : "Restarting ";
        const std::string done = start ? "Started " : stop ? "Stopped " : "Restarted ";
        run_in_background(
            busy + unit + "...",
            [this, start, stop, unit, password] {
                if (start)
                    return service_manager_->start_service(unit, password);
                if (stop)
                    return service_manager_->stop_service(unit, password);
                return service_manager_->restart_service(unit, password);
            },
            done + unit, "Failed to " + std::string(start ? "start " : stop ? "stop " : "restart ") + unit);
    }

    void App::run_in_background(const std::string &busy_message, std::function<std::pair<bool, std::string>()> work,
                                const std::string &success_message, const std::string &failure_prefix)
    {
        busy_ = true;
        busy_message_ = busy_message;
        status_message_.clear();
        error_message_.clear();

        if (worker_.joinable())
        {
            worker_.join();
        }
        worker_ = std::thread([this, work = std::move(work), success_message, failure_prefix] {
            auto result = work();
            screen_.Post([this, result, success_message, failure_prefix] {
                busy_ = false;
                busy_message_.clear();
                refresh_services();
                load_service_details();
                if (result.first)
                {
                    status_message_ = success_message;
                }
                else
                {
                    error_message_ = failure_prefix + ": " + result.second;
                }
            });
            screen_.PostEvent(Event::Custom);
        });
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

    void App::daemon_reload(const std::string &password)
    {
        run_in_background(
            "Reloading systemd daemon...", [this, password] { return service_manager_->daemon_reload(password); },
            "Daemon reloaded successfully.", "Daemon reload failed");
    }

    void App::edit_selected_service()
    {
        const auto *target = action_target();
        if (!target)
        {
            return;
        }

        const auto service = *target;
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
            load_service_details();
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
        case AuthAction::Start:
            auth_dialog_message_ = "Start " + service.unit;
            break;
        case AuthAction::Stop:
            auth_dialog_message_ = "Stop " + service.unit;
            break;
        case AuthAction::Restart:
            auth_dialog_message_ = "Restart " + service.unit;
            break;
        case AuthAction::DaemonReload:
            auth_dialog_message_ = "Reload systemd daemon";
            break;
        case AuthAction::None:
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

        if (action == AuthAction::Start || action == AuthAction::Stop || action == AuthAction::Restart)
        {
            run_service_action(action, service, password);
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

    void App::open_service_detail(bool logs_full)
    {
        const auto *selected = selected_service();
        if (!selected)
        {
            return;
        }
        const auto service = *selected;
        logs_full_ = logs_full;
        if (detail_open_ && detail_unit_ == service.unit)
        {
            return;
        }

        detail_open_ = true;
        detail_unit_ = service.unit;
        service_summary_.reset();
        status_message_.clear();
        error_message_.clear();
        load_service_details();
        service_metrics_ = ServiceMetrics{};
        set_sampler_unit(service.unit);

        service_logs_.open(service.unit, system_mode_
                                             ? "Starting: journalctl -u " + service.unit + " -f -n 100"
                                             : "Starting: journalctl --user-unit " + service.unit + " -f -n 100");
        if (!logging_manager_)
        {
            service_logs_.append("No log source available on this platform");
            return;
        }
        auto stream = logging_manager_->stream_logs(service.unit, system_mode_, service_logs_.sink());
        if (!stream)
        {
            service_logs_.append("Unable to start log stream");
        }
        service_logs_.attach(std::move(stream));
    }

    void App::load_service_details()
    {
        if (!detail_open_)
        {
            return;
        }
        try
        {
            service_details_ = service_manager_->describe_service(detail_unit_);
            service_details_error_.clear();
        }
        catch (const std::exception &e)
        {
            service_details_.clear();
            service_details_error_ = std::string("Could not read details: ") + e.what();
        }
    }

    void App::set_sampler_unit(const std::string &unit)
    {
        {
            std::lock_guard<std::mutex> lock(sampler_mutex_);
            sampler_unit_ = unit;
            ++sampler_generation_;
        }
        sampler_cv_.notify_all();
    }

    void App::sampler_loop()
    {
        using clock = std::chrono::steady_clock;
        const auto start = clock::now();
        while (true)
        {
            std::string unit;
            int generation = 0;
            {
                std::unique_lock<std::mutex> lock(sampler_mutex_);
                sampler_cv_.wait(lock, [this] { return sampler_stop_ || !sampler_unit_.empty(); });
                if (sampler_stop_)
                {
                    return;
                }
                unit = sampler_unit_;
                generation = sampler_generation_;
            }

            ServiceDetails details;
            std::string details_error;
            try
            {
                details = service_manager_->describe_service(unit);
            }
            catch (const std::exception &e)
            {
                details_error = std::string("Could not read details: ") + e.what();
            }
            auto sample = service_manager_->sample_service(unit);
            const double seconds = std::chrono::duration<double>(clock::now() - start).count();

            screen_.Post([this, unit, details = std::move(details), details_error, sample, seconds]() mutable {
                if (!detail_open_ || detail_unit_ != unit)
                {
                    return;
                }
                if (!busy_)
                {
                    // An action in flight reloads the details itself when done.
                    service_details_ = std::move(details);
                    service_details_error_ = details_error;
                }
                record_service_sample(sample, seconds);
            });
            screen_.PostEvent(Event::Custom);

            std::unique_lock<std::mutex> lock(sampler_mutex_);
            sampler_cv_.wait_for(lock, std::chrono::seconds(2),
                                 [this, generation] { return sampler_stop_ || sampler_generation_ != generation; });
            if (sampler_stop_)
            {
                return;
            }
        }
    }

    void App::record_service_sample(const ServiceSample &sample, double seconds)
    {
        auto &m = service_metrics_;
        if (sample.cpu_nsec >= 0)
        {
            m.has_cpu = true;
            // CPU nanoseconds per second -> percent of one core.
            const double rate = m.cpu_rate.update(sample.cpu_nsec, seconds);
            if (rate >= 0)
            {
                m.cpu.push(rate / 1e7);
            }
        }
        if (sample.memory_bytes >= 0)
        {
            m.has_memory = true;
            m.memory.push(sample.memory_bytes);
        }
        if (sample.tasks >= 0)
        {
            m.has_tasks = true;
            m.tasks.push(sample.tasks);
        }
        if (sample.io_read_bytes >= 0 || sample.io_write_bytes >= 0)
        {
            m.has_io = true;
            const double read = sample.io_read_bytes >= 0 ? m.read_rate.update(sample.io_read_bytes, seconds) : -1;
            const double write = sample.io_write_bytes >= 0 ? m.write_rate.update(sample.io_write_bytes, seconds) : -1;
            if (read >= 0 || write >= 0)
            {
                m.read_per_s = read;
                m.write_per_s = write;
                m.io.push(std::max(0.0, read) + std::max(0.0, write));
            }
        }
    }

    void App::close_service_detail()
    {
        set_sampler_unit("");
        service_logs_.close();
        detail_open_ = false;
        logs_full_ = false;
        detail_unit_.clear();
        service_details_.clear();
        service_details_error_.clear();
    }

    bool App::services_back()
    {
        if (logs_full_)
        {
            logs_full_ = false;
            return true;
        }
        if (detail_open_)
        {
            close_service_detail();
            status_message_.clear();
            error_message_.clear();
            return true;
        }
        return false;
    }

    void App::switch_tab(Tab tab)
    {
        active_tab_ = tab;
        containers_view_->set_active(tab == Tab::Containers);
        screen_.PostEvent(Event::Custom);
    }

    bool App::handle_services_event(Event event)
    {
        auto clear_messages = [this] {
            status_message_.clear();
            error_message_.clear();
        };
        const bool up = event == Event::ArrowUp || event == Event::Special("\x1b" "k");
        const bool down = event == Event::ArrowDown || event == Event::Special("\x1b" "j");

        // Enter drills down: list -> detail page -> full-size logs (and back).
        if (event == Event::Return || event == Event::Special("\x1b" "f")) {
            if (detail_open_) {
                logs_full_ = !logs_full_;
                // The log frame settles on the second layout pass after a size change.
                screen_.PostEvent(Event::Custom);
            } else if (event == Event::Return) {
                open_service_detail(false);
            }
            return true;
        }
        if (event == Event::Special("\x1b" "l")) {
            open_service_detail(true);
            return true;
        }
        if (event == Event::Special("\x1b" "o")) {
            request_service_action(AuthAction::Start);
            return true;
        }
        if (event == Event::Special("\x1b" "x")) {
            request_service_action(AuthAction::Stop);
            return true;
        }
        if (event == Event::Special("\x1b" "r")) {
            request_service_action(AuthAction::Restart);
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
        if (event == Event::Special("\x1b" "c")) {
            close_service_detail();
            clear_messages();
            return true;
        }
        if (detail_open_) {
            if (event == Event::PageUp || event == Event::PageDown) {
                service_logs_.scroll(event == Event::PageUp ? -10 : 10);
                return true;
            }
            // Arrows scroll the summary, or the logs when they fill the page.
            if (up || down) {
                if (logs_full_) {
                    service_logs_.scroll(up ? -1 : 1);
                } else {
                    service_summary_.scroll(up ? -1 : 1);
                }
                return true;
            }
            // The list is hidden on the detail page, so typing does not change the filter there.
            if (event.is_character() || event == Event::Backspace) {
                return true;
            }
        } else {
            if (up || down) {
                selected_index_ = std::clamp(selected_index_ + (up ? -1 : 1), 0,
                                             std::max(0, static_cast<int>(filtered_services_.size()) - 1));
                clear_messages();
                return true;
            }
            if (event.is_character()) {
                filter_text_ += event.character();
                apply_filter();
                clear_messages();
                return true;
            }
            if (event == Event::Backspace && !filter_text_.empty()) {
                filter_text_.pop_back();
                apply_filter();
                clear_messages();
                return true;
            }
        }
        if (event.is_mouse()) {
            auto& mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
                const int delta = mouse.button == Mouse::WheelUp ? -1 : 1;
                if (!detail_open_) {
                    selected_index_ = std::clamp(selected_index_ + delta, 0,
                                                 std::max(0, static_cast<int>(filtered_services_.size()) - 1));
                    clear_messages();
                } else if (service_logs_.contains(mouse.x, mouse.y)) {
                    service_logs_.scroll(delta * 3);
                } else if (service_summary_.contains(mouse.x, mouse.y)) {
                    service_summary_.scroll(delta * 3);
                }
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                if (reload_button_box_.Contain(mouse.x, mouse.y)) {
                    request_daemon_reload();
                    return true;
                }
                switch (service_actions_.hit(mouse.x, mouse.y)) {
                case ActionBar::Action::Start:
                    request_service_action(AuthAction::Start);
                    return true;
                case ActionBar::Action::Stop:
                    request_service_action(AuthAction::Stop);
                    return true;
                case ActionBar::Action::Restart:
                    request_service_action(AuthAction::Restart);
                    return true;
                case ActionBar::Action::Back:
                    services_back();
                    return true;
                case ActionBar::Action::ToggleLogs:
                    logs_full_ = !logs_full_;
                    screen_.PostEvent(Event::Custom);
                    return true;
                case ActionBar::Action::ToggleGraphs:
                    graphs_.toggle();
                    return true;
                case ActionBar::Action::None:
                    break;
                }
                if (detail_open_) {
                    return true;
                }
                for (size_t i = 0; i < item_boxes_.size(); ++i) {
                    if (item_boxes_[i].Contain(mouse.x, mouse.y)) {
                        // Clicking the selected row opens it, like Enter.
                        if (static_cast<int>(i) == selected_index_) {
                            open_service_detail(false);
                        } else {
                            selected_index_ = static_cast<int>(i);
                        }
                        clear_messages();
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
        if (auth_dialog_open_ || confirm_.is_open() || containers_view_->edit_dialog_open()) {
            return false;
        }
        if (show_help_) {
            return handle_help_event(event);
        }
        if (event == Event::Escape) {
            // Close the innermost thing first; quit only from the plain list.
            if (active_tab_ == Tab::Containers && containers_view_->back()) {
            } else if (active_tab_ == Tab::Services && services_back()) {
            } else if (active_tab_ == Tab::Containers && !containers_view_->filter_text().empty()) {
                containers_view_->set_filter("");
            } else if (active_tab_ == Tab::Services && !filter_text_.empty()) {
                filter_text_.clear();
                apply_filter();
            } else {
                screen_.Exit();
            }
            return true;
        }
        if (event == Event::Special("\x1b" "q")) {
            screen_.Exit();
            return true;
        }
        if (event == Event::Special("\x1b" "g")) {
            graphs_.toggle();
            return true;
        }
        if (event == Event::Character('?')) {
            open_help();
            return true;
        }
        if (event == Event::Tab || event == Event::TabReverse) {
            switch_tab(active_tab_ == Tab::Services ? Tab::Containers : Tab::Services);
            return true;
        }
        if (event == Event::Special("\x1b" "1")) {
            switch_tab(Tab::Containers);
            return true;
        }
        if (event == Event::Special("\x1b" "2")) {
            switch_tab(Tab::Services);
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
                open_help();
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
        main_component |= Modal(containers_view_->add_project_dialog(), containers_view_->add_project_dialog_open_flag());
        main_component |= Modal(confirm_.component(), confirm_.open_flag());
        containers_view_->set_confirm([this](const std::string &question, const std::string &label,
                                             std::function<void()> on_confirm) {
            confirm_.ask(question, label, std::move(on_confirm));
        });
        return main_component;
    }

    Element App::render()
    {
        const bool containers = active_tab_ == Tab::Containers;
        const auto &status = containers ? containers_view_->status_message() : status_message_;
        const auto &error = containers ? containers_view_->error_message() : error_message_;

        service_actions_.hide();
        Elements layout;
        layout.push_back(render_top_bar());
        layout.push_back(separator() | color(BorderColor()));
        if (containers)
        {
            layout.push_back(containers_view_->render_body() | flex);
        }
        else if (detail_open_)
        {
            layout.push_back(render_service_detail());
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
        const bool on_detail = containers ? containers_view_->detail_open() : detail_open_;
        const bool on_full_logs = containers ? containers_view_->logs_full() : logs_full_;
        const auto view_hints = containers ? containers_view_->hints() : std::string();
        const std::string hints =
            !view_hints.empty() ? view_hints
            : on_full_logs ? "  ·  ↑↓ PgUp/PgDn scroll  ·  Enter shrink logs  ·  Esc back  ·  ? help  "
            : on_detail  ? "  ·  ↑↓ scroll info  ·  PgUp/PgDn scroll logs  ·  Enter expand logs  ·  Esc back  ·  ? help  "
                         : "  ·  ↑↓ move  ·  Enter open  ·  Alt+o start  ·  Alt+x stop  ·  Alt+r restart  ·  ? help  ";
        const auto summary = containers ? containers_view_->summary()
                                        : std::to_string(filtered_services_.size()) + "/" + std::to_string(services_.size()) + " services";
        layout.push_back(hbox({
            text(" Filter: ") | color(MutedColor()),
            text(filter + "█") | color(AccentColor()),
            filler(),
            text(summary) | color(MutedColor()),
            text(hints) | color(MutedColor()),
        }));

        auto page = vbox(std::move(layout)) | borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor());
        auto dialog_open = show_help_ || auth_dialog_open_ || confirm_.is_open() || containers_view_->edit_dialog_open();
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
            tab("Containers", Tab::Containers, containers_tab_box_),
            text("│") | color(BorderColor()),
            tab("Services", Tab::Services, services_tab_box_),
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

        for (size_t i = 0; i < filtered_services_.size(); ++i)
        {
            const auto &svc = filtered_services_[i];
            bool selected = (static_cast<int>(i) == selected_index_);
            bool active = service_is_active(svc);
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

    Element App::render_service_detail()
    {
        const auto *service = action_target();
        const bool active = service && service_is_active(*service);
        const std::string state = !service ? "? unknown" : active ? "● active" : "○ inactive";
        auto actions = service_actions_.render(state, active ? Colors::running_fg() : Colors::stopped_fg(), active);
        auto title = detail_title_bar(detail_unit_, service ? service->description : "", std::move(actions));
        Element summary = logs_full_ ? text("") : service_summary_.render(render_service_summary());
        return detail_page(std::move(title), std::move(summary),
                           service_logs_.render(service_actions_.render_log_header(logs_full_, graphs_.shown(), graphs_fit())),
                           logs_full_, logs_full_ || !graphs_.shown() ? nullptr : render_service_graphs());
    }

    Element App::render_service_graphs()
    {
        const auto &m = service_metrics_;
        std::vector<MetricView> views;
        if (m.has_cpu)
        {
            views.push_back({"CPU", format_metric_percent(m.cpu.last()), &m.cpu, format_metric_percent, 1.0,
                             graph_colors::Cpu()});
        }
        if (m.has_memory)
        {
            views.push_back({"Memory", format_metric_bytes(m.memory.last()), &m.memory, format_metric_bytes,
                             1024.0 * 1024.0, graph_colors::Memory(), false});
        }
        if (m.has_tasks)
        {
            views.push_back({"Tasks", format_metric_count(m.tasks.last()), &m.tasks, format_metric_count, 1.0,
                             graph_colors::Tasks(), false});
        }
        if (m.has_io)
        {
            auto rate = [](double v) { return v < 0 ? std::string("-") : format_metric_rate(v); };
            views.push_back({"Disk I/O", "R " + rate(m.read_per_s) + "  W " + rate(m.write_per_s), &m.io,
                             format_metric_rate, 1024.0, graph_colors::Disk()});
        }
        if (views.empty())
        {
            return nullptr;
        }
        return service_graphs_.render(views);
    }

    Elements App::render_service_summary()
    {
        Elements lines;
        if (!service_details_error_.empty())
        {
            lines.push_back(paragraph(" " + service_details_error_) | color(Colors::error_fg()));
        }
        for (const auto &[label, value] : service_details_)
        {
            // Already in the title bar.
            if (label == "Description")
            {
                continue;
            }
            if (label == "Command" || label == "ProgramArguments" || label == "Program")
            {
                lines.push_back(command_row(label, value));
            }
            else
            {
                lines.push_back(field_row(label, value));
            }
        }
        // Live usage from the last list refresh (ps), or basic fields if `show` failed.
        if (const auto *service = action_target())
        {
            if (service_details_.empty())
            {
                lines.push_back(field_row("State", service->active + " (" + service->sub + ")"));
                lines.push_back(field_row("PID", service->pid));
            }
            if (!service->cpu.empty() || !service->memory.empty())
            {
                lines.push_back(field_row("Usage", "CPU " + (service->cpu.empty() ? "-" : service->cpu) + " · RSS " +
                                                       (service->memory.empty() ? "-" : service->memory)));
            }
        }
        return lines;
    }

    void App::open_help()
    {
        help_tab_ = active_tab_ == Tab::Containers ? HelpTab::Containers : HelpTab::Services;
        show_help_ = true;
    }

    bool App::handle_help_event(Event event)
    {
        auto step = [this](int delta) {
            help_tab_ = static_cast<HelpTab>((static_cast<int>(help_tab_) + delta + kHelpTabs) % kHelpTabs);
        };
        if (event == Event::Special("\x1b" "q"))
        {
            screen_.Exit();
        }
        else if (event == Event::Escape || event == Event::Character('?'))
        {
            show_help_ = false;
        }
        else if (event == Event::Tab || event == Event::ArrowRight)
        {
            step(1);
        }
        else if (event == Event::TabReverse || event == Event::ArrowLeft)
        {
            step(-1);
        }
        else if (event.is_character() && event.character()[0] >= '1' && event.character()[0] < '1' + kHelpTabs)
        {
            help_tab_ = static_cast<HelpTab>(event.character()[0] - '1');
        }
        else if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed)
        {
            for (int i = 0; i < kHelpTabs; ++i)
            {
                if (help_tab_boxes_[i].Contain(event.mouse().x, event.mouse().y))
                    help_tab_ = static_cast<HelpTab>(i);
            }
        }
        // Everything else is swallowed so nothing behind the dialog reacts.
        return true;
    }

    Element App::render_help()
    {
        auto key = [](const std::string &keys, const std::string &action) {
            return hbox({text("  " + keys) | color(AccentColor()) | size(WIDTH, EQUAL, 23), text(action)});
        };
        auto heading = [](const std::string &title) { return text(title) | bold; };
        auto gap = [] { return text(""); };

        Elements body;
        switch (help_tab_)
        {
        case HelpTab::Containers:
            body = {
                heading("Containers"),
                key("Enter / Alt+i", "Open detail page (summary + logs)"),
                key("Alt+l", "Open with full-size logs"),
                key("Alt+o / x / r", "Start / stop / restart (stop, restart ask y/n)"),
                key("Alt+u", "Edit resource limits (docker/podman update)"),
                key("Alt+s", "Show/hide secret env values"),
                key("Alt+Shift+r", "Refresh now"),
                gap(),
                heading("Compose projects (on a project row or page)"),
                key("← / →", "Fold / unfold"),
                key("Enter", "Project page: services, merged logs, graphs"),
                key("Alt+o / x / r", "Start (up -d when down) / stop / restart"),
                key("Alt+u", "up -d: apply config and image changes"),
                key("Alt+b", "up -d --build"),
                key("Alt+p", "Pull images"),
                key("Alt+d", "Down (forget, if already down)"),
                key("Alt+a", "Add a project by path"),
                key("Alt+v", "From a container to its project"),
            };
            break;
        case HelpTab::Services:
            body = {
                heading("Services"),
                key("Enter", "Open detail page (summary + logs)"),
                key("Alt+l", "Open with full-size logs"),
                key("Alt+o / x / r", "Start / stop / restart (stop, restart ask y/n)"),
                key("Alt+e", "Edit unit file ($GSYSTEMCTL_EDITOR, default vim)"),
                key("Alt+Shift+r", "systemctl daemon-reload"),
                gap(),
                text("When sudo needs a password, a prompt replaces the y/n confirmation.") | color(MutedColor()),
            };
            break;
        case HelpTab::General:
            body = {
                heading("List"),
                key("↑↓ / Alt+k/j", "Move selection"),
                key("type / Backspace", "Filter"),
                key("Enter / click", "Open detail page"),
                gap(),
                heading("Detail page"),
                key("Enter / Alt+f", "Expand / shrink logs"),
                key("Alt+g", "Graphs on/off (replace the info below 100 cols)"),
                key("↑↓", "Scroll summary (logs when expanded)"),
                key("PgUp / PgDn", "Scroll logs"),
                key("Esc", "Back one level"),
                gap(),
                heading("App"),
                key("Tab", "Switch tab"),
                key("Alt+1 / Alt+2", "Containers / Services"),
                key("Esc", "Back, clear filter, then quit"),
                key("Alt+q", "Quit"),
            };
            break;
        case HelpTab::Settings:
            body = {
                heading("Startup options"),
                key("(none)", "Open the Containers tab; Services without docker or podman"),
                key("-s, --services", "Open the Services tab"),
                key("-c, --containers", "Open the Containers tab, even without a runtime"),
                key("--user", "User services instead of system services"),
                key("-f, --filter TEXT", "Initial filter"),
                gap(),
                heading("Environment"),
                key("GSYSTEMCTL_RUNTIME", "docker, podman or auto (default: auto, docker first)"),
                key("GSYSTEMCTL_DOCKER", "Container CLI binary (default: docker or podman)"),
                key("GSYSTEMCTL_COMPOSE", "Compose command (default: <runtime> compose)"),
                key("GSYSTEMCTL_EDITOR", "Editor for unit files (default: vim)"),
                gap(),
                heading("Files"),
                key("compose-projects", "Saved compose projects, in"),
                text("                         $XDG_CONFIG_HOME/g-systemctl or ~/.config/g-systemctl"),
            };
            break;
        }

        static const char *const labels[kHelpTabs] = {"Containers", "Services", "General", "Settings"};
        Elements tabs;
        for (int i = 0; i < kHelpTabs; ++i)
        {
            const bool active = static_cast<int>(help_tab_) == i;
            auto label = text(" " + std::to_string(i + 1) + " " + labels[i] + " ");
            label = active ? label | color(Color::White) | bold | bgcolor(Colors::selected_bg())
                           : label | color(MutedColor());
            tabs.push_back(label | reflect(help_tab_boxes_[i]));
            if (i + 1 < kHelpTabs)
                tabs.push_back(text("│") | color(BorderColor()));
        }

        return vbox({
                   hbox({text(" Help ") | bold | color(AccentColor()), filler(), hbox(std::move(tabs))}),
                   separator() | color(BorderColor()),
                   // Fixed size, so switching tabs does not resize the dialog.
                   vbox(std::move(body)) | size(HEIGHT, EQUAL, 19) | size(WIDTH, EQUAL, 86),
                   separator() | color(BorderColor()),
                   text(" Tab / ←→ / 1-4 switch  ·  Esc or ? close") | color(MutedColor()),
               }) |
               borderStyled(ROUNDED, BorderColor()) | bgcolor(BackgroundColor()) | center;
    }

} // namespace gsystemctl::ui
