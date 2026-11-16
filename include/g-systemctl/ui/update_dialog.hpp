#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/screen/box.hpp>
#include <functional>
#include <string>

namespace gsystemctl::ui
{

    // Offers a new release (y update · n not now · s skip this version · d stop
    // checking), then shows how the install went. Like ConfirmDialog, Enter
    // does not accept the offer.
    class UpdateDialog
    {
    public:
        enum class Choice
        {
            Update,
            NotNow,
            Skip,
            StopChecking,
        };

        // `blocker`: why this install cannot update itself (e.g. Homebrew); empty if it can.
        void offer(const std::string &version, const std::string &current, const std::string &blocker,
                   std::function<void(Choice)> on_choice);
        // After an install: the outcome; any key closes it.
        void show_result(bool ok, const std::string &message);

        bool is_open() const { return open_; }
        const bool *open_flag() const { return &open_; }
        // Wire with Modal(component(), open_flag()).
        ftxui::Component component();

    private:
        bool open_ = false;
        bool result_ = false; // showing an outcome rather than an offer
        bool ok_ = false;
        std::string version_;
        std::string current_;
        std::string blocker_;
        std::string message_;
        std::function<void(Choice)> on_choice_;
        ftxui::Box update_box_;
        ftxui::Box later_box_;

        void choose(Choice choice);
    };

} // namespace gsystemctl::ui
