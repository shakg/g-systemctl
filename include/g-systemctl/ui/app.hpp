#pragma once

#include "g-systemctl/core/service_manager.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/core/service.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gsystemctl::ui
{

    class App
    {
    public:
        App(bool system_mode = false, const std::string &initial_filter = "");
        int run();

    private:
        ftxui::ScreenInteractive screen_;
        std::shared_ptr<ServiceManager> service_manager_;
        std::unique_ptr<LoggingManager> logging_manager_;
        bool system_mode_;

        std::vector<ServiceUnit> services_;
        std::vector<ServiceUnit> filtered_services_;
        std::string filter_text_;
        int selected_index_ = 0;
        std::string status_message_;
        std::string error_message_;
        std::string log_unit_;
        std::vector<std::string> log_lines_;
        mutable std::mutex log_mutex_;
        bool log_panel_open_ = false;
        bool show_help_ = false;
        std::vector<ftxui::Box> item_boxes_;
        std::vector<ftxui::Box> toggle_button_boxes_;
        std::vector<ftxui::Box> log_button_boxes_;
        std::unique_ptr<LogStream> log_stream_;

        void refresh_services();
        void apply_filter();
        void toggle_selected_service();
        void open_logs_for_selected_service();
        void close_logs();
        ftxui::Component create_main_component();
        ftxui::Element render();
        ftxui::Element render_service_list();
        ftxui::Element render_log_panel();
        ftxui::Element render_status_bar();
        ftxui::Element render_help();
    };

} // namespace gsystemctl::ui
