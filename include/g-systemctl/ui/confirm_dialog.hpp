#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/screen/box.hpp>
#include <functional>
#include <string>

namespace gsystemctl::ui
{

    // Yes/no prompt for disruptive actions (stop, restart). Only `y` or a click
    // on the confirm button runs the action; Enter does nothing, so a stray
    // keypress cannot confirm. `n`, Esc or the cancel button dismiss it.
    class ConfirmDialog
    {
    public:
        void ask(const std::string &question, const std::string &confirm_label, std::function<void()> on_confirm);

        bool is_open() const { return open_; }
        const bool *open_flag() const { return &open_; }
        // Wire with Modal(component(), open_flag()).
        ftxui::Component component();

    private:
        bool open_ = false;
        std::string question_;
        std::string confirm_label_;
        std::function<void()> on_confirm_;
        ftxui::Box confirm_box_;
        ftxui::Box cancel_box_;

        void confirm();
        void cancel();
    };

} // namespace gsystemctl::ui
