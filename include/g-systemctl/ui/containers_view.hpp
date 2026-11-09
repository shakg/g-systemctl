#pragma once

#include "g-systemctl/container/compose_store.hpp"
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
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gsystemctl::ui
{

    // The Containers tab: list with live stats, grouped by compose project; a
    // detail page per container or project (summary plus a log strip that can
    // expand to full size); and an editor for resource limits. All methods run
    // on the UI thread; runtime calls that may block run on background threads.
    class ContainersView
    {
    public:
        // Shows a yes/no prompt; `on_confirm` runs on the UI thread if accepted.
        using ConfirmFn = std::function<void(const std::string &question, const std::string &confirm_label,
                                             std::function<void()> on_confirm)>;

        // `compose_store_path`: where seen compose projects are saved; empty keeps them in memory.
        ContainersView(ftxui::ScreenInteractive &screen, std::shared_ptr<ContainerManager> manager,
                       const std::string &compose_store_path = ComposeStore::default_path());
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
        // Runs on the UI thread once, if the first runtime check fails.
        void set_on_unavailable(std::function<void()> on_unavailable) { on_unavailable_ = std::move(on_unavailable); }
        bool detail_open() const { return detail_open_; }
        // Key hints for the footer; empty to use App's defaults.
        std::string hints() const;
        bool logs_full() const { return logs_full_; }
        // Goes up one level (full logs -> detail -> list). False if already at the list.
        bool back();

        // Returns true if the event was consumed.
        bool handle_event(ftxui::Event event);

        ftxui::Element render_body();
        // Modal for editing resource limits; App wires it with Modal().
        ftxui::Component edit_dialog();
        bool *edit_dialog_open_flag() { return &edit_open_; }
        // Modal for adding a compose project by path; App wires it with Modal().
        ftxui::Component add_project_dialog();
        bool *add_project_dialog_open_flag() { return &add_open_; }

        // Any of this view's modals.
        bool edit_dialog_open() const { return edit_open_ || add_open_; }
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
        std::function<void()> on_unavailable_;

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

        // Compose. Projects come from container labels plus the saved list.
        ComposeStore compose_store_;
        std::vector<ComposeProject> projects_;
        bool compose_ok_ = false;
        std::string compose_message_;
        // Project -> services whose config changed since their containers were created.
        std::map<std::string, std::vector<std::string>> drift_;
        // Project -> why the drift check failed (e.g. compose file gone).
        std::map<std::string, std::string> drift_error_;
        std::set<std::string> collapsed_;
        // "project/service" keys of services with more than one replica.
        std::set<std::string> multi_replica_;
        // Summed over the project's running containers.
        struct ProjectMetrics
        {
            MetricHistory cpu, memory, pids, net, block;
            double net_rx_per_s = -1, net_tx_per_s = -1, read_per_s = -1, write_per_s = -1;
        };
        std::map<std::string, ProjectMetrics> project_metrics_;

        // Visible list rows. `index` points into projects_ or filtered_.
        struct ListRow
        {
            enum class Kind
            {
                Project,
                Container,
                Divider,
            } kind;
            int index = -1;
            bool last_child = false; // draws └ instead of ├
        };
        std::vector<ListRow> rows_;
        int selected_index_ = 0; // into rows_
        // Container ID, or "project:" + name.
        std::string selected_key_;
        std::string status_message_;
        std::string error_message_;
        bool busy_ = false;
        std::string busy_message_;
        std::vector<ftxui::Box> item_boxes_;

        // Detail page. It shows logs_id_ (a container) or detail_project_ (a
        // compose project); `docker logs -f` exits when the container stops,
        // so the stream is reattached once it runs again.
        bool detail_open_ = false;
        std::string detail_project_;
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

        bool add_open_ = false;
        std::string add_path_;
        int add_cursor_ = 0;
        std::string add_error_;

        // Background work.
        std::thread action_thread_;
        std::thread poller_;
        std::mutex poll_mutex_;
        std::condition_variable poll_cv_;
        bool poll_stop_ = false;
        bool poll_active_ = false;
        bool poll_requested_ = false;
        // Read by the poller: project whose drift is checked on every poll.
        std::string poll_project_;
        bool poll_drift_all_ = true;
        // Poller thread only.
        bool poller_compose_checked_ = false;
        bool poller_compose_ok_ = false;
        int poll_count_ = 0;

        void poll_loop();
        void poll_once(bool &runtime_ok);
        void set_containers(std::vector<Container> containers);
        void set_stats(std::map<std::string, ContainerStats> stats);
        // `first_if_gone`: if the selected row is filtered out, select the first row (typing a filter).
        void apply_filter(bool first_if_gone = false);
        // `step`: direction to skip dividers in (+1 down, -1 up).
        void select(int index, int step = 1);
        const Container *selected() const;
        const ComposeProject *selected_project() const;
        const ComposeProject *find_project(const std::string &name) const;
        const Container *find_container(const std::string &id) const;
        void set_poll_project(const std::string &name, bool check_all);
        void set_drift(std::map<std::string, std::vector<std::string>> drift, std::map<std::string, std::string> errors,
                       bool all);
        void remember_projects();
        void clear_messages();

        void run_action(const std::string &busy_message, std::function<ContainerManager::Result()> action,
                        const std::string &success_message);
        // The container on the open detail page, else the selected one.
        const Container *action_target() const;
        void start_target();
        void stop_target();
        void restart_target();
        // Compose actions on the project on the open page, else the selected one.
        const ComposeProject *project_target() const;
        // project_target(), or explains why there is none. May set status_message_.
        const ComposeProject *require_project();
        void run_compose(const ComposeProject &project, ComposeAction action);
        void confirm_compose(const ComposeProject &project, ComposeAction action, const std::string &question,
                             const std::string &label);
        void compose_up_target();
        void compose_build_target();
        void compose_pull_target();
        void compose_down_target();
        void fold_selected(bool collapse);
        void open_detail(bool logs_full);
        void open_project_detail(const std::string &name, bool logs_full);
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
        void open_add_dialog();
        void submit_add_dialog();
        void close_add_dialog();

        ftxui::Element render_list();
        ftxui::Element render_project_row(const ComposeProject &project, bool is_selected);
        ftxui::Element render_container_row(const Container &c, bool is_selected, bool child, bool last_child);
        ftxui::Element render_detail();
        ftxui::Element render_project_detail();
        ftxui::Elements render_summary();
        ftxui::Elements render_project_summary(const ComposeProject &project);
        ftxui::Element render_graphs();
        ftxui::Element render_project_graphs();
        void record_stats();
    };

} // namespace gsystemctl::ui
