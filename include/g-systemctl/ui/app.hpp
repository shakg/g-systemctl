#pragma once

#include "g-systemctl/core/service_manager.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/core/service.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/ui/action_bar.hpp"
#include "g-systemctl/ui/confirm_dialog.hpp"
#include "g-systemctl/ui/containers_view.hpp"
#include "g-systemctl/ui/detail_page.hpp"
#include "g-systemctl/ui/log_panel.hpp"
#include "g-systemctl/ui/metric_graph.hpp"
#include <condition_variable>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gsystemctl::ui
{

    class App
    {
    public:
        enum class Tab
        {
            Services,
            Containers,
        };

        App(bool system_mode = false, const std::string &initial_filter = "", Tab initial_tab = Tab::Services);
        ~App();
        int run();

    private:
        ftxui::ScreenInteractive screen_;
        std::shared_ptr<CommandExecutor> executor_;
        std::shared_ptr<ServiceManager> service_manager_;
        std::unique_ptr<LoggingManager> logging_manager_;
        bool system_mode_;

        std::vector<ServiceUnit> services_;
        std::vector<ServiceUnit> filtered_services_;
        std::string filter_text_;
        int selected_index_ = 0;
        std::string status_message_;
        std::string error_message_;
        // Detail page: summary plus a log strip that can fill the page.
        // Actions target detail_unit_ while it is open.
        bool detail_open_ = false;
        bool logs_full_ = false;
        std::string detail_unit_;
        ServiceDetails service_details_;
        std::string service_details_error_;
        SummaryPane service_summary_;
        // Graphs of the detail page, fed by sampler_ every 2s while it is open.
        struct ServiceMetrics
        {
            MetricHistory cpu, memory, tasks, io;
            CounterRate cpu_rate, read_rate, write_rate;
            double read_per_s = -1, write_per_s = -1;
            bool has_cpu = false, has_memory = false, has_tasks = false, has_io = false;
        };
        ServiceMetrics service_metrics_;
        // Alt+g; shared by both tabs.
        GraphToggle graphs_;
        GraphPanel service_graphs_;
        std::thread sampler_;
        std::mutex sampler_mutex_;
        std::condition_variable sampler_cv_;
        std::string sampler_unit_; // empty: idle
        int sampler_generation_ = 0;
        bool sampler_stop_ = false;
        LogPanel service_logs_;
        ActionBar service_actions_;
        ConfirmDialog confirm_;
        bool show_help_ = false;
        ftxui::Box shortcuts_box_;
        Tab active_tab_ = Tab::Services;
        std::unique_ptr<ContainersView> containers_view_;
        ftxui::Box services_tab_box_;
        ftxui::Box containers_tab_box_;
        bool auth_dialog_open_ = false;
        std::string auth_password_;
        std::string auth_dialog_message_;
        enum class AuthAction
        {
            None,
            Start,
            Stop,
            Restart,
            DaemonReload,
        };
        AuthAction pending_auth_action_ = AuthAction::None;
        ServiceUnit pending_auth_service_;
        std::vector<ftxui::Box> item_boxes_;
        ftxui::Box reload_button_box_;
        // Set while a background operation runs; all input is ignored meanwhile.
        bool busy_ = false;
        std::string busy_message_;
        std::thread worker_;

        void refresh_services();
        void apply_filter();
        const ServiceUnit *selected_service() const;
        // The unit on the open detail page, else the selected one.
        const ServiceUnit *action_target() const;
        // `action` is Start, Stop or Restart. Stop and Restart ask for confirmation.
        void request_service_action(AuthAction action);
        void run_service_action(AuthAction action, const ServiceUnit& service, const std::string& password = "");
        void run_in_background(const std::string& busy_message, std::function<std::pair<bool, std::string>()> work,
                               const std::string& success_message, const std::string& failure_prefix);
        void request_daemon_reload();
        void daemon_reload(const std::string& password = "");
        void edit_selected_service();
        void open_auth_dialog(AuthAction action, const ServiceUnit& service = ServiceUnit{});
        void submit_auth_dialog();
        void close_auth_dialog();
        void open_service_detail(bool logs_full);
        void close_service_detail();
        void load_service_details();
        void sampler_loop();
        // Points the sampler at `unit`, or idles it if empty.
        void set_sampler_unit(const std::string &unit);
        void record_service_sample(const ServiceSample &sample, double seconds);
        // Goes up one level (full logs -> detail -> list). False if already at the list.
        bool services_back();
        void switch_tab(Tab tab);
        bool handle_services_event(ftxui::Event event);
        ftxui::Component create_main_component();
        ftxui::Element render();
        ftxui::Element render_top_bar();
        ftxui::Element render_tabs();
        ftxui::Element render_shortcuts_dialog();
        ftxui::Element render_service_list();
        ftxui::Element render_service_detail();
        ftxui::Elements render_service_summary();
        // Null when there is nothing to graph.
        ftxui::Element render_service_graphs();
        ftxui::Element render_help();
    };

} // namespace gsystemctl::ui
