#include "g-systemctl/ui/containers_view.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ftxui/component/component_options.hpp>

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    namespace
    {
        constexpr auto kPollInterval = std::chrono::seconds(2);
        constexpr int kLogTail = 200;

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return value;
        }

        struct StateStyle
        {
            std::string label;
            Color fg;
        };

        StateStyle state_style(const std::string &state)
        {
            if (state == "running")
                return {"● running", Colors::running_fg()};
            if (state == "paused")
                return {"◐ paused", WarningColor()};
            if (state == "restarting")
                return {"↻ restarting", WarningColor()};
            if (state == "dead")
                return {"✕ dead", Colors::error_fg()};
            return {"○ " + state, Colors::stopped_fg()};
        }

        bool looks_secret(const std::string &env_entry)
        {
            auto key = lower(env_entry.substr(0, env_entry.find('=')));
            for (const char *marker : {"pass", "secret", "token", "key", "credential", "auth"})
            {
                if (key.find(marker) != std::string::npos)
                    return true;
            }
            return false;
        }

        // Unix timestamp with nanoseconds, the format `docker logs --since` takes.
        std::string unix_time_now()
        {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld.%09lld", static_cast<long long>(ns / 1000000000),
                          static_cast<long long>(ns % 1000000000));
            return buffer;
        }

        std::string limit_text(const std::string &value, const std::string &unlimited = "unlimited")
        {
            return value.empty() ? unlimited : value;
        }
    } // namespace

    ContainersView::ContainersView(ScreenInteractive &screen, std::shared_ptr<ContainerManager> manager)
        : screen_(screen), manager_(std::move(manager)), logs_(screen)
    {
        poller_ = std::thread([this] { poll_loop(); });
    }

    ContainersView::~ContainersView()
    {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_stop_ = true;
        }
        poll_cv_.notify_all();
        if (poller_.joinable())
            poller_.join();
        if (action_thread_.joinable())
            action_thread_.join();
    }

    // ---- polling ------------------------------------------------------------

    void ContainersView::set_active(bool active)
    {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_active_ = active;
            poll_requested_ = active;
        }
        poll_cv_.notify_all();
    }

    void ContainersView::refresh_now()
    {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_requested_ = true;
        }
        poll_cv_.notify_all();
    }

    void ContainersView::poll_loop()
    {
        bool runtime_ok = false;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(poll_mutex_);
                poll_cv_.wait(lock, [this] { return poll_stop_ || poll_active_; });
                if (poll_stop_)
                    return;
                poll_requested_ = false;
            }

            poll_once(runtime_ok);

            {
                std::unique_lock<std::mutex> lock(poll_mutex_);
                poll_cv_.wait_for(lock, kPollInterval, [this] { return poll_stop_ || poll_requested_; });
                if (poll_stop_)
                    return;
            }
        }
    }

    void ContainersView::poll_once(bool &runtime_ok)
    {
        if (!runtime_ok)
        {
            auto [ok, message] = manager_->check_available();
            runtime_ok = ok;
            screen_.Post([this, ok = ok, message = message] {
                checked_ = true;
                available_ = ok;
                availability_message_ = message;
            });
            screen_.PostEvent(Event::Custom);
            if (!ok)
                return;
        }

        try
        {
            auto listed_at = unix_time_now();
            auto containers = manager_->list_containers();
            screen_.Post([this, containers = std::move(containers), listed_at]() mutable {
                set_containers(std::move(containers));
                reattach_logs_if_running(listed_at);
            });
            screen_.PostEvent(Event::Custom);
        }
        catch (const std::exception &e)
        {
            runtime_ok = false;
            std::string message = e.what();
            screen_.Post([this, message] {
                available_ = false;
                availability_message_ = message;
            });
            screen_.PostEvent(Event::Custom);
            return;
        }

        try
        {
            auto stats = manager_->container_stats();
            screen_.Post([this, stats = std::move(stats)]() mutable {
                set_stats(std::move(stats));
            });
            screen_.PostEvent(Event::Custom);
        }
        catch (const std::exception &)
        {
            // Stats are best-effort; the list is still useful without them.
        }
    }

    void ContainersView::set_containers(std::vector<Container> containers)
    {
        containers_ = std::move(containers);
        for (auto &c : containers_)
        {
            auto it = stats_.find(c.id);
            c.stats = (it != stats_.end() && c.is_running()) ? it->second : ContainerStats{};
        }
        apply_filter();
    }

    void ContainersView::set_stats(std::map<std::string, ContainerStats> stats)
    {
        stats_ = std::move(stats);
        set_containers(std::move(containers_));
    }

    // ---- selection / filter ---------------------------------------------------

    void ContainersView::set_filter(const std::string &filter)
    {
        filter_text_ = filter;
        apply_filter();
    }

    void ContainersView::apply_filter()
    {
        filtered_.clear();
        auto needle = lower(filter_text_);
        for (const auto &c : containers_)
        {
            if (needle.empty() || lower(c.name).find(needle) != std::string::npos ||
                lower(c.image).find(needle) != std::string::npos ||
                lower(c.compose_project).find(needle) != std::string::npos)
            {
                filtered_.push_back(c);
            }
        }

        // Keep the same container selected across refreshes and re-sorts.
        auto it = std::find_if(filtered_.begin(), filtered_.end(),
                               [this](const Container &c) { return c.id == selected_id_; });
        if (it != filtered_.end())
        {
            selected_index_ = static_cast<int>(it - filtered_.begin());
        }
        else
        {
            selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(filtered_.size()) - 1));
            selected_id_ = filtered_.empty() ? "" : filtered_[selected_index_].id;
        }
    }

    void ContainersView::select(int index)
    {
        if (filtered_.empty())
            return;
        selected_index_ = std::clamp(index, 0, static_cast<int>(filtered_.size()) - 1);
        selected_id_ = filtered_[selected_index_].id;
    }

    const Container *ContainersView::selected() const
    {
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(filtered_.size()))
            return nullptr;
        return &filtered_[selected_index_];
    }

    void ContainersView::clear_messages()
    {
        status_message_.clear();
        error_message_.clear();
    }

    std::string ContainersView::summary() const
    {
        return std::to_string(filtered_.size()) + "/" + std::to_string(containers_.size()) + " containers";
    }

    // ---- actions --------------------------------------------------------------

    void ContainersView::run_action(const std::string &busy_message, std::function<ContainerManager::Result()> action,
                                    const std::string &success_message)
    {
        if (busy_)
            return;
        busy_ = true;
        busy_message_ = busy_message;
        clear_messages();

        if (action_thread_.joinable())
            action_thread_.join();
        action_thread_ = std::thread([this, action = std::move(action), success_message, busy_message] {
            auto [ok, message] = action();
            screen_.Post([this, ok = ok, message = message, success_message, busy_message] {
                busy_ = false;
                busy_message_.clear();
                if (ok)
                {
                    status_message_ = success_message;
                }
                else
                {
                    error_message_ = busy_message.substr(0, busy_message.size() - 3) + " failed: " + message;
                }
                if (detail_open_)
                    reload_details();
                refresh_now();
            });
            screen_.PostEvent(Event::Custom);
        });
    }

    const Container *ContainersView::action_target() const
    {
        auto find = [this](auto pred) -> const Container * {
            auto it = std::find_if(containers_.begin(), containers_.end(), pred);
            return it == containers_.end() ? nullptr : &*it;
        };
        if (!detail_open_)
            return selected();
        if (const auto *c = find([this](const Container &c) { return c.id == logs_id_; }))
            return c;
        // Recreated with a new ID and not reattached yet.
        return find([this](const Container &c) { return c.name == logs_name_; });
    }

    void ContainersView::start_target()
    {
        const auto *c = action_target();
        if (!c)
            return;
        clear_messages();
        if (c->is_active())
        {
            status_message_ = c->name + " is already running";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        run_action("Starting " + name + "...", [this, id] { return manager_->start(id); }, "Started " + name);
    }

    void ContainersView::stop_target()
    {
        const auto *c = action_target();
        if (!c)
            return;
        clear_messages();
        if (!c->is_active())
        {
            status_message_ = c->name + " is not running (Alt+o to start)";
            return;
        }
        if (!confirm_)
        {
            error_message_ = "Stop needs confirmation, which is unavailable";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        confirm_("Stop container " + name + "?", "Stop", [this, id, name] {
            run_action("Stopping " + name + "...", [this, id] { return manager_->stop(id); }, "Stopped " + name);
        });
    }

    void ContainersView::restart_target()
    {
        const auto *c = action_target();
        if (!c)
            return;
        clear_messages();
        if (!c->is_active())
        {
            status_message_ = c->name + " is not running (Alt+o to start)";
            return;
        }
        if (!confirm_)
        {
            error_message_ = "Restart needs confirmation, which is unavailable";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        confirm_("Restart container " + name + "?", "Restart", [this, id, name] {
            run_action("Restarting " + name + "...", [this, id] { return manager_->restart(id); }, "Restarted " + name);
        });
    }

    void ContainersView::open_detail(bool logs_full)
    {
        const auto *c = selected();
        if (!c)
            return;
        logs_full_ = logs_full;
        if (detail_open_ && logs_id_ == c->id)
            return;
        clear_messages();
        detail_open_ = true;
        summary_.reset();
        logs_id_ = c->id;
        logs_name_ = c->name;
        details_ = ContainerDetails{};
        reload_details();
        logs_.open(c->name + " (" + c->id + ")",
                   "Starting: " + manager_->runtime_name() + " logs -f --tail " + std::to_string(kLogTail) + " " + c->name);
        attach_logs(kLogTail, "", false);
    }

    void ContainersView::close_detail()
    {
        stop_logs();
        detail_open_ = false;
        logs_full_ = false;
        details_ = ContainerDetails{};
        details_error_.clear();
    }

    bool ContainersView::back()
    {
        if (logs_full_)
        {
            logs_full_ = false;
            return true;
        }
        if (detail_open_)
        {
            close_detail();
            clear_messages();
            return true;
        }
        return false;
    }

    void ContainersView::attach_logs(int tail, const std::string &since, bool resume)
    {
        const int generation = ++logs_generation_;
        logs_waiting_ = false;
        // Docker can report a container as running for a moment after its log
        // stream closed, so a resumed stream may end at once with no output.
        // Announce the restart only once output actually arrives.
        auto got_line = std::make_shared<std::atomic<bool>>(false);
        auto announce = resume ? "── " + logs_name_ + " started ──" : std::string();
        auto on_line = [this, got_line, announce](std::string line) {
            if (!got_line->exchange(true) && !announce.empty())
                logs_.append(announce);
            logs_.append(std::move(line));
        };
        auto on_end = [this, generation, got_line, resume] {
            auto ended_at = unix_time_now();
            bool quiet_retry = resume && !got_line->load();
            screen_.Post([this, generation, ended_at, quiet_retry] { on_logs_ended(generation, ended_at, quiet_retry); });
            screen_.PostEvent(Event::Custom);
        };
        auto stream = manager_->stream_logs(logs_id_, tail, since, on_line, on_end);
        if (!stream)
        {
            error_message_ = "Unable to start log stream";
        }
        logs_.attach(std::move(stream));
    }

    void ContainersView::stop_logs()
    {
        ++logs_generation_;
        logs_waiting_ = false;
        logs_id_.clear();
        logs_name_.clear();
        logs_.close();
    }

    void ContainersView::on_logs_ended(int generation, const std::string &ended_at, bool quiet_retry)
    {
        if (generation != logs_generation_ || !detail_open_)
            return;
        logs_waiting_ = true;
        if (quiet_retry)
        {
            // Keep the original end time so no lines are skipped; retry on the next poll.
            return;
        }
        logs_ended_at_ = ended_at;
        logs_.append("── " + logs_name_ + " is not running; logs resume when it starts ──");
        refresh_now();
    }

    void ContainersView::reattach_logs_if_running(const std::string &listed_at)
    {
        // A list fetched before the stream ended may still show the container running.
        if (!detail_open_ || !logs_waiting_ || std::stold(listed_at) <= std::stold(logs_ended_at_))
            return;
        auto it = std::find_if(containers_.begin(), containers_.end(),
                               [this](const Container &c) { return c.id == logs_id_; });
        if (it == containers_.end())
        {
            // Recreated (e.g. `docker compose up`): same name, new ID.
            it = std::find_if(containers_.begin(), containers_.end(),
                              [this](const Container &c) { return c.name == logs_name_; });
            if (it == containers_.end())
                return;
            logs_id_ = it->id;
            logs_.set_title(it->name + " (" + it->id + ")");
            reload_details();
        }
        if (!it->is_running())
            return;
        attach_logs(-1, logs_ended_at_, true);
    }

    void ContainersView::reload_details()
    {
        if (logs_id_.empty())
            return;
        try
        {
            details_ = manager_->inspect(logs_id_);
            if (details_.id.size() > 12)
                details_.id.resize(12);
            details_error_.clear();
        }
        catch (const std::exception &e)
        {
            // Container may have been removed; keep the last snapshot.
            details_error_ = "Inspect failed: " + std::string(e.what());
        }
    }

    // ---- resource editor -------------------------------------------------------

    void ContainersView::open_edit_dialog()
    {
        const auto *c = action_target();
        if (!c)
            return;
        try
        {
            auto details = manager_->inspect(c->id);
            edit_target_id_ = c->id;
            edit_target_name_ = c->name;
            edit_original_ = details.resource_settings();
            edit_values_ = edit_original_;
            const std::string *fields[] = {&edit_values_.cpus, &edit_values_.memory, &edit_values_.memory_swap,
                                           &edit_values_.pids_limit, &edit_values_.restart_policy};
            for (int i = 0; i < 5; ++i)
                edit_cursors_[i] = static_cast<int>(fields[i]->size());
            edit_error_.clear();
            edit_open_ = true;
            clear_messages();
        }
        catch (const std::exception &e)
        {
            error_message_ = "Inspect failed: " + std::string(e.what());
        }
    }

    void ContainersView::submit_edit_dialog()
    {
        auto result = build_resource_update(edit_original_, edit_values_);
        if (!result.error.empty())
        {
            edit_error_ = result.error;
            return;
        }
        auto id = edit_target_id_;
        auto name = edit_target_name_;
        close_edit_dialog();
        if (result.update.empty())
        {
            status_message_ = "No changes for " + name;
            return;
        }
        auto update = result.update;
        run_action("Updating " + name + "...", [this, id, update] { return manager_->update_resources(id, update); },
                   "Updated resources of " + name);
    }

    void ContainersView::close_edit_dialog()
    {
        edit_open_ = false;
        edit_error_.clear();
        screen_.PostEvent(Event::Custom);
    }

    Component ContainersView::edit_dialog()
    {
        auto make_input = [](std::string *value, const std::string &placeholder, int *cursor) {
            InputOption option;
            option.multiline = false;
            option.cursor_position = cursor;
            return Input(value, placeholder, option);
        };
        auto cpus = make_input(&edit_values_.cpus, "unlimited", &edit_cursors_[0]);
        auto memory = make_input(&edit_values_.memory, "unlimited", &edit_cursors_[1]);
        auto swap = make_input(&edit_values_.memory_swap, "default", &edit_cursors_[2]);
        auto pids = make_input(&edit_values_.pids_limit, "unlimited", &edit_cursors_[3]);
        auto restart = make_input(&edit_values_.restart_policy, "no", &edit_cursors_[4]);
        auto inputs = ftxui::Container::Vertical({cpus, memory, swap, pids, restart});

        auto row = [](const std::string &label, Component input, const std::string &hint) {
            return hbox({
                text(label) | color(HeaderColor()) | size(WIDTH, EQUAL, 15),
                text("[") | color(BorderColor()),
                input->Render() | color(TextColor()) | size(WIDTH, EQUAL, 16),
                text("]") | color(BorderColor()),
                text(" " + hint) | color(MutedColor()),
            });
        };

        auto renderer = Renderer(inputs, [this, cpus, memory, swap, pids, restart, row] {
            Elements lines = {
                hbox({text("Resources  ") | bold | color(AccentColor()), text(edit_target_name_) | color(WarningColor())}),
                separator() | color(BorderColor()),
                row("CPUs", cpus, "e.g. 0.5, 2"),
                row("Memory", memory, "e.g. 256m, 2g"),
                row("Memory+swap", swap, "-1 = unlimited swap"),
                row("PIDs limit", pids, "empty = unlimited"),
                row("Restart", restart, "no|always|unless-stopped|on-failure[:N]"),
                separator() | color(BorderColor()),
                text("Applied live with `docker update`. Env, ports and volumes") | color(MutedColor()),
                text("need the container to be recreated.") | color(MutedColor()),
            };
            if (!edit_error_.empty())
            {
                lines.push_back(paragraph(edit_error_) | color(Colors::error_fg()));
            }
            lines.push_back(separator() | color(BorderColor()));
            lines.push_back(text("Tab/↑↓ field  Enter apply  Esc cancel") | color(MutedColor()));
            return vbox(std::move(lines)) | size(WIDTH, LESS_THAN, 80) | borderStyled(ROUNDED, BorderColor()) |
                   bgcolor(PanelColor());
        });

        return CatchEvent(renderer, [this](Event event) {
            if (event == Event::Return)
            {
                submit_edit_dialog();
                return true;
            }
            if (event == Event::Escape)
            {
                close_edit_dialog();
                return true;
            }
            return false;
        });
    }

    // ---- events ---------------------------------------------------------------

    bool ContainersView::handle_event(Event event)
    {
        if (event == Event::Special("\x1b" "R"))
        {
            refresh_now();
            status_message_ = "Refreshing...";
            return true;
        }
        if (event == Event::Special("\x1b" "c"))
        {
            close_detail();
            clear_messages();
            return true;
        }
        if (detail_open_)
        {
            if (event == Event::Special("\x1b" "s"))
            {
                show_secrets_ = !show_secrets_;
                return true;
            }
            if (event == Event::PageUp || event == Event::PageDown)
            {
                logs_.scroll(event == Event::PageUp ? -10 : 10);
                return true;
            }
            // Arrows scroll the summary, or the logs when they fill the page.
            const bool up = event == Event::ArrowUp || event == Event::Special("\x1b" "k");
            if (up || event == Event::ArrowDown || event == Event::Special("\x1b" "j"))
            {
                if (logs_full_)
                    logs_.scroll(up ? -1 : 1);
                else
                    summary_.scroll(up ? -1 : 1);
                return true;
            }
        }
        else if (event == Event::ArrowUp || event == Event::Special("\x1b" "k") || event == Event::ArrowDown ||
                 event == Event::Special("\x1b" "j"))
        {
            const bool up = event == Event::ArrowUp || event == Event::Special("\x1b" "k");
            select(selected_index_ + (up ? -1 : 1));
            clear_messages();
            return true;
        }

        if (!available_)
        {
            return false;
        }

        // Actions are ignored while a previous one is still running.
        const bool is_action = event == Event::Special("\x1b" "o") || event == Event::Special("\x1b" "x") ||
                               event == Event::Special("\x1b" "r") || event == Event::Special("\x1b" "u");
        if (is_action && busy_)
            return true;

        // Enter drills down: list -> detail page -> full-size logs (and back).
        if (event == Event::Return || event == Event::Special("\x1b" "f"))
        {
            if (detail_open_)
            {
                logs_full_ = !logs_full_;
                // The log frame settles on the second layout pass after a size change.
                screen_.PostEvent(Event::Custom);
            }
            else if (event == Event::Return)
                open_detail(false);
            return true;
        }
        if (event == Event::Special("\x1b" "l"))
        {
            open_detail(true);
            return true;
        }
        if (event == Event::Special("\x1b" "i"))
        {
            open_detail(false);
            return true;
        }
        if (event == Event::Special("\x1b" "o"))
        {
            start_target();
            return true;
        }
        if (event == Event::Special("\x1b" "x"))
        {
            stop_target();
            return true;
        }
        if (event == Event::Special("\x1b" "r"))
        {
            restart_target();
            return true;
        }
        if (event == Event::Special("\x1b" "u"))
        {
            open_edit_dialog();
            return true;
        }
        if (event.is_character() || event == Event::Backspace)
        {
            // The list is hidden on the detail page, so typing does not change the filter there.
            if (detail_open_)
                return true;
            if (event.is_character())
                filter_text_ += event.character();
            else if (!filter_text_.empty())
                filter_text_.pop_back();
            apply_filter();
            clear_messages();
            return true;
        }
        if (event.is_mouse())
        {
            auto &mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown)
            {
                int delta = mouse.button == Mouse::WheelUp ? -1 : 1;
                if (!detail_open_)
                    select(selected_index_ + delta);
                else if (logs_.contains(mouse.x, mouse.y))
                    logs_.scroll(delta * 3);
                else if (summary_.contains(mouse.x, mouse.y))
                    summary_.scroll(delta * 3);
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed)
            {
                switch (actions_.hit(mouse.x, mouse.y))
                {
                case ActionBar::Action::Start:
                    if (!busy_)
                        start_target();
                    return true;
                case ActionBar::Action::Stop:
                    if (!busy_)
                        stop_target();
                    return true;
                case ActionBar::Action::Restart:
                    if (!busy_)
                        restart_target();
                    return true;
                case ActionBar::Action::Back:
                    back();
                    return true;
                case ActionBar::Action::ToggleLogs:
                    logs_full_ = !logs_full_;
                    screen_.PostEvent(Event::Custom);
                    return true;
                case ActionBar::Action::None:
                    break;
                }
                if (detail_open_)
                    return true;
                for (size_t i = 0; i < item_boxes_.size(); ++i)
                {
                    if (item_boxes_[i].Contain(mouse.x, mouse.y))
                    {
                        // Clicking the selected row opens it, like Enter.
                        if (static_cast<int>(i) == selected_index_)
                            open_detail(false);
                        else
                            select(static_cast<int>(i));
                        clear_messages();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ---- rendering -------------------------------------------------------------

    Element ContainersView::render_body()
    {
        actions_.hide();
        if (!checked_)
        {
            return text("Connecting to " + manager_->runtime_name() + "...") | color(MutedColor()) | center | flex;
        }
        if (!available_)
        {
            return vbox({
                       text(manager_->runtime_name() + " is not available") | bold | color(WarningColor()) | center,
                       text(""),
                       paragraph(availability_message_) | color(MutedColor()) | center,
                       text(""),
                       text("Start the daemon, check socket permissions (docker group), or set GSYSTEMCTL_DOCKER.") |
                           color(MutedColor()) | center,
                   }) |
                   center | flex;
        }
        return detail_open_ ? render_detail() : render_list() | flex;
    }

    Element ContainersView::render_detail()
    {
        const auto *c = action_target();
        Element actions = c ? actions_.render(state_style(c->state).label, state_style(c->state).fg, c->is_active())
                            : actions_.render("✕ removed", Colors::error_fg(), false);
        auto title = detail_title_bar(logs_name_, c ? c->image : details_.image, std::move(actions));
        Element summary = logs_full_ ? text("") : summary_.render(render_summary());
        return detail_page(std::move(title), std::move(summary), logs_.render(actions_.render_logs_toggle(logs_full_)),
                           logs_full_);
    }

    Element ContainersView::render_list()
    {
        if (filtered_.empty())
        {
            item_boxes_.clear();
            return text(containers_.empty() ? "No containers" : "No containers match the filter") | center | dim;
        }

        auto header = hbox({
                          text(" "),
                          flex_cell("CONTAINER", HeaderColor()),
                          cell("IMAGE", 24, HeaderColor()),
                          cell("STATE", 13, HeaderColor()),
                          cell("STATUS", 22, HeaderColor()),
                          right_cell("CPU", 8, HeaderColor()),
                          right_cell("MEM", 22, HeaderColor()),
                          right_cell("PIDS", 6, HeaderColor()),
                          text(" "),
                      }) |
                      bgcolor(PanelColor());

        Elements rows;
        item_boxes_.resize(filtered_.size());
        for (size_t i = 0; i < filtered_.size(); ++i)
        {
            const auto &c = filtered_[i];
            bool is_selected = static_cast<int>(i) == selected_index_;
            auto style = state_style(c.state);
            auto dash = [](const std::string &v) { return v.empty() ? std::string("-") : v; };

            auto row = hbox({
                flex_cell(c.name, is_selected ? Color::White : TextColor(), is_selected),
                cell(c.image, 24, MutedColor()),
                cell(style.label, 13, style.fg, c.is_running()),
                cell(c.status, 22, MutedColor()),
                right_cell(dash(c.stats.cpu), 8, MutedColor()),
                right_cell(dash(c.stats.mem_usage), 22, MutedColor()),
                right_cell(dash(c.stats.pids), 6, MutedColor()),
                text(" "),
            });
            if (is_selected)
            {
                row = hbox({text("▌") | color(AccentColor()), row | flex}) | bgcolor(Colors::selected_bg()) | focus;
            }
            else
            {
                row = hbox({text(" "), row | flex});
            }
            rows.push_back(row | reflect(item_boxes_[i]));
        }

        return vbox({
                   header,
                   vbox(std::move(rows)) | vscroll_indicator | yframe | flex,
               }) |
               flex;
    }

    Elements ContainersView::render_summary()
    {
        const auto &d = details_;
        const auto *c = action_target();
        auto settings = d.resource_settings();
        Elements lines;
        auto section = [&lines](const std::string &title) { lines.push_back(section_title(title)); };

        if (!details_error_.empty())
            lines.push_back(paragraph(" " + details_error_) | color(Colors::error_fg()));

        lines.push_back(section_title("Overview", true));
        if (c)
        {
            auto style = state_style(c->state);
            lines.push_back(field_row("State", style.label + " · " + c->status, style.fg));
            if (c->is_running() && !c->stats.cpu.empty())
            {
                lines.push_back(field_row("CPU", c->stats.cpu));
                lines.push_back(field_row("Memory", c->stats.mem_usage + "  (" + c->stats.mem_percent + ")"));
                lines.push_back(field_row("PIDs", c->stats.pids));
                lines.push_back(field_row("Net I/O", c->stats.net_io));
                lines.push_back(field_row("Block I/O", c->stats.block_io));
            }
        }
        lines.push_back(field_row("ID", d.id));
        lines.push_back(field_row("Image", d.image));
        lines.push_back(field_row("Command", d.entrypoint.empty() ? d.command : d.entrypoint + " " + d.command));
        lines.push_back(field_row("Started", d.started_at + (d.restart_count.empty() ? "" : " · restarts " + d.restart_count)));
        lines.push_back(field_row("Created", d.created));
        if (!d.compose_project.empty())
        {
            lines.push_back(field_row("Compose", d.compose_project + (d.compose_dir.empty() ? "" : "  (" + d.compose_dir + ")")));
        }

        section("Resources (Alt+u to edit)");
        lines.push_back(field_row("CPUs", limit_text(settings.cpus)));
        lines.push_back(field_row("Memory limit", d.memory > 0 ? format_bytes(d.memory) : "unlimited"));
        lines.push_back(field_row("Memory+swap", d.memory_swap < 0 ? "unlimited" : d.memory_swap == 0 ? "default" : format_bytes(d.memory_swap)));
        lines.push_back(field_row("PIDs limit", limit_text(settings.pids_limit)));
        lines.push_back(field_row("Restart", settings.restart_policy));

        section("Network");
        for (const auto &p : d.ports)
            lines.push_back(field_row("Port", p));
        for (const auto &n : d.networks)
            lines.push_back(field_row("Network", n));
        if (d.ports.empty() && d.networks.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section("Mounts");
        for (const auto &m : d.mounts)
            lines.push_back(text(" " + m) | color(TextColor()));
        if (d.mounts.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section(std::string("Environment (read-only; recreate to change)  Alt+s ") + (show_secrets_ ? "hide" : "show") + " secrets");
        for (const auto &e : d.env)
        {
            std::string shown = e;
            if (!show_secrets_ && looks_secret(e))
            {
                shown = e.substr(0, e.find('=')) + "=••••••";
            }
            lines.push_back(text(" " + shown) | color(TextColor()));
        }
        if (d.env.empty())
            lines.push_back(text(" none") | color(MutedColor()));
        return lines;
    }

} // namespace gsystemctl::ui
