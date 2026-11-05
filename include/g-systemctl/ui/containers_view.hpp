#pragma once

#include "g-systemctl/container/container_manager.hpp"
#include "g-systemctl/ui/action_bar.hpp"
#include "g-systemctl/ui/detail_page.hpp"
#include "g-systemctl/ui/log_panel.hpp"
#include "g-systemctl/ui/metric_graph.hpp"
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

    // The Containers tab: list with live stats, a detail page (summary plus a
    // log strip that can expand to full size) and an editor for resource limits. All methods run on the UI thread;
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
        // Graphs on/off state, owned by App and toggled by both tabs.
        void set_graph_toggle(GraphToggle *graphs) { graphs_ = graphs; }
        bool detail_open() const { return detail_open_; }
        bool logs_full() const { return logs_full_; }
        // Goes up one level (full logs -> detail -> list). False if already at the list.
        bool back();

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
        // Graph history per running container, keyed by ID; fed by set_stats.
        struct ContainerMetrics
        {
            MetricHistory cpu, memory, pids, net, block;
            CounterRate net_rx, net_tx, block_read, block_write;
            double net_rx_per_s = -1, net_tx_per_s = -1, read_per_s = -1, write_per_s = -1;
        };
        std::map<std::string, ContainerMetrics> metrics_;
        std::string filter_text_;
        int selected_index_ = 0;
        std::string selected_id_;
        std::string status_message_;
        std::string error_message_;
        bool busy_ = false;
        std::string busy_message_;
        std::vector<ftxui::Box> item_boxes_;

        // Detail page. It shows logs_id_; `docker logs -f` exits when the
        // container stops, so the stream is reattached once it runs again.
        bool detail_open_ = false;
        bool logs_full_ = false;
        LogPanel logs_;
        std::string logs_id_;
        std::string logs_name_;
        // Bumped on every attach/detach; end notifications from older streams are ignored.
        int logs_generation_ = 0;
        bool logs_waiting_ = false;
        std::string logs_ended_at_;
        ContainerDetails details_;
        std::string details_error_;
        SummaryPane summary_;
        ActionBar actions_;
        bool show_secrets_ = false;
        GraphToggle *graphs_ = nullptr;
        GraphPanel graph_panel_;

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
        // The container on the open detail page, else the selected one.
        const Container *action_target() const;
        void start_target();
        void stop_target();
        void restart_target();
        void open_detail(bool logs_full);
        void close_detail();
        // `resume`: reattaching after the container restarted.
        void attach_logs(int tail, const std::string &since, bool resume);
        void stop_logs();
        void on_logs_ended(int generation, const std::string &ended_at, bool quiet_retry);
        // `listed_at`: when the container list was fetched (unix time).
        void reattach_logs_if_running(const std::string &listed_at);
        void reload_details();
        void open_edit_dialog();
        void submit_edit_dialog();
        void close_edit_dialog();

        ftxui::Element render_list();
        ftxui::Element render_detail();
        ftxui::Elements render_summary();
        ftxui::Element render_graphs();
        void record_stats();
    };

} // namespace gsystemctl::ui
