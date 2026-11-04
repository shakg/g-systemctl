#pragma once

#include "g-systemctl/container/container_manager.hpp"
#include "g-systemctl/ui/action_bar.hpp"
#include "g-systemctl/ui/log_panel.hpp"
#include <condition_variable>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gsystemctl::ui
{

    // The Containers tab: list with live stats, log streaming, inspect panel
    // and an editor for resource limits. All methods run on the UI thread;
    // runtime calls that may block run on background threads.
    class ContainersView
    {
    public:
        // Shows a yes/no prompt; `on_confirm` runs on the UI thread if accepted.
        using ConfirmFn = std::function<void(const std::string &question, const std::string &confirm_label,
                                             std::function<void()> on_confirm)>;

        ContainersView(ftxui::ScreenInteractive &screen, std::shared_ptr<ContainerManager> manager);
        ~ContainersView();

        ContainersView(const ContainersView &) = delete;
        ContainersView &operator=(const ContainersView &) = delete;

        // Polling runs only while the tab is visible.
        void set_active(bool active);
        void set_filter(const std::string &filter);
        void refresh_now();
        // Stop and restart go through this prompt; without one they are refused.
        void set_confirm(ConfirmFn confirm) { confirm_ = std::move(confirm); }
        bool panel_open() const { return panel_ != Panel::None; }
        void close_panel();

        // Returns true if the event was consumed.
        bool handle_event(ftxui::Event event);

        ftxui::Element render_body();
        // Modal for editing resource limits; App wires it with Modal().
        ftxui::Component edit_dialog();
        bool *edit_dialog_open_flag() { return &edit_open_; }

        bool edit_dialog_open() const { return edit_open_; }
        bool busy() const { return busy_; }
        const std::string &busy_message() const { return busy_message_; }
        const std::string &status_message() const { return status_message_; }
        const std::string &error_message() const { return error_message_; }
        const std::string &filter_text() const { return filter_text_; }
        std::string summary() const;

    private:
        enum class Panel
        {
            None,
            Logs,
            Details,
        };

        ftxui::ScreenInteractive &screen_;
        std::shared_ptr<ContainerManager> manager_;
        ConfirmFn confirm_;

        // UI-thread state.
        bool available_ = false;
        bool checked_ = false;
        std::string availability_message_;
        std::vector<Container> containers_;
        std::vector<Container> filtered_;
        std::map<std::string, ContainerStats> stats_;
        std::string filter_text_;
        int selected_index_ = 0;
        std::string selected_id_;
        std::string status_message_;
        std::string error_message_;
        bool busy_ = false;
        std::string busy_message_;
        std::vector<ftxui::Box> item_boxes_;

        Panel panel_ = Panel::None;
        LogPanel logs_;
        // Container the log panel follows. `docker logs -f` exits when the
        // container stops, so the stream is reattached once it runs again.
        std::string logs_id_;
        std::string logs_name_;
        // Bumped on every attach/detach; end notifications from older streams are ignored.
        int logs_generation_ = 0;
        bool logs_waiting_ = false;
        std::string logs_ended_at_;
        ContainerDetails details_;
        // Full ID of the inspected container (details_.id is shortened for display).
        std::string details_target_id_;
        ActionBar actions_;
        int details_scroll_ = 0;
        int details_line_count_ = 0;
        bool show_secrets_ = false;
        ftxui::Box details_box_;

        bool edit_open_ = false;
        std::string edit_target_id_;
        std::string edit_target_name_;
        ResourceSettings edit_original_;
        ResourceSettings edit_values_;
        // Cursor of each input, placed at the end of the prefilled value.
        int edit_cursors_[5] = {0, 0, 0, 0, 0};
        std::string edit_error_;

        // Background work.
        std::thread action_thread_;
        std::thread poller_;
        std::mutex poll_mutex_;
        std::condition_variable poll_cv_;
        bool poll_stop_ = false;
        bool poll_active_ = false;
        bool poll_requested_ = false;

        void poll_loop();
        void poll_once(bool &runtime_ok);
        void set_containers(std::vector<Container> containers);
        void set_stats(std::map<std::string, ContainerStats> stats);
        void apply_filter();
        void select(int index);
        const Container *selected() const;
        void clear_messages();

        void run_action(const std::string &busy_message, std::function<ContainerManager::Result()> action,
                        const std::string &success_message);
        // The container in the open logs/details panel, else the selected one.
        const Container *action_target() const;
        void start_target();
        void stop_target();
        void restart_target();
        ftxui::Element render_actions();
        void open_logs();
        // `resume`: reattaching after the container restarted.
        void attach_logs(int tail, const std::string &since, bool resume);
        void stop_logs();
        void on_logs_ended(int generation, const std::string &ended_at, bool quiet_retry);
        // `listed_at`: when the container list was fetched (unix time).
        void reattach_logs_if_running(const std::string &listed_at);
        void open_details();
        void reload_details();
        void scroll_details(int delta);
        void open_edit_dialog();
        void submit_edit_dialog();
        void close_edit_dialog();

        ftxui::Element render_list();
        ftxui::Element render_details();
    };

} // namespace gsystemctl::ui
