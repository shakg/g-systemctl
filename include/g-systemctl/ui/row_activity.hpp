#pragma once

#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <chrono>
#include <ftxui/component/animation.hpp>
#include <ftxui/dom/elements.hpp>
#include <map>
#include <string>

namespace gsystemctl::ui
{

    // Progress and outcome of an action (start, stop, ...) shown in the list row
    // it acts on, next to the top-bar spinner and the status line, which are
    // easy to miss. Rows are keyed by the caller (container ID, unit name, ...).
    // A finished action stays visible for a few seconds. UI thread only.
    class RowActivity
    {
    public:
        // E.g. {"stopping", "stopped", "stop failed"}.
        struct Labels
        {
            std::string busy, done, failed;
        };

        // `label`: what is running, e.g. "stopping".
        void start(const std::string &key, const std::string &label)
        {
            entries_[key] = {State::Busy, label, {}};
        }

        // `label`: the outcome, e.g. "stopped" or "stop failed".
        void finish(const std::string &key, bool ok, const std::string &label)
        {
            const auto keep = ok ? kKeepDone : kKeepFailed;
            entries_[key] = {ok ? State::Done : State::Failed, label, Clock::now() + keep};
        }

        // Null when the row has nothing to show.
        ftxui::Element render(const std::string &key)
        {
            using namespace ftxui;
            using namespace theme;
            auto it = entries_.find(key);
            if (it == entries_.end())
                return nullptr;
            const auto &e = it->second;
            const auto now = Clock::now();
            if (e.state != State::Busy && now >= e.until)
            {
                entries_.erase(it);
                return nullptr;
            }
            // Keeps the spinner turning and clears the outcome when it expires,
            // also where no poller redraws the screen.
            animation::RequestAnimationFrame();
            switch (e.state)
            {
            case State::Busy:
            {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                return hbox({spinner(15, static_cast<size_t>(ms / 80)), text(" " + e.label)}) | color(WarningColor()) |
                       bold;
            }
            case State::Done:
                return text("✓ " + e.label) | color(Colors::running_fg()) | bold;
            case State::Failed:
                return text("✕ " + e.label) | color(Colors::error_fg()) | bold;
            }
            return nullptr;
        }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr auto kKeepDone = std::chrono::seconds(6);
        static constexpr auto kKeepFailed = std::chrono::seconds(12);

        enum class State
        {
            Busy,
            Done,
            Failed,
        };
        struct Entry
        {
            State state;
            std::string label;
            Clock::time_point until;
        };
        std::map<std::string, Entry> entries_;
    };

} // namespace gsystemctl::ui
