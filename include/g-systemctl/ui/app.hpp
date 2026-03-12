#pragma once

#include "g-systemctl/core/service_manager.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include "g-systemctl/core/service.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/box.hpp>
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
        bool show_help_ = false;
        std::vector<ftxui::Box> item_boxes_;

        void refresh_services();
        void apply_filter();
        void toggle_selected_service();
        void open_logs_for_selected_service();
        ftxui::Component create_main_component();
        ftxui::Element render();
        ftxui::Element render_service_list();
        ftxui::Element render_status_bar();
        ftxui::Element render_help();
    };

} // namespace gsystemctl::ui
