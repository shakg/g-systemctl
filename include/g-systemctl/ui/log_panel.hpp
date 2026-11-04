#pragma once

#include "g-systemctl/core/logging_manager.hpp"
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gsystemctl::ui
{

    // Scrollable, follow-the-tail view over a LogStream. append() may be
    // called from any thread; everything else from the UI thread.
    class LogPanel
    {
    public:
        explicit LogPanel(ftxui::ScreenInteractive &screen, size_t max_lines = 2000);
        ~LogPanel();

        // Stops any current stream and shows an empty panel titled `title`.
        void open(const std::string &title, const std::string &first_line = "");
        // Takes ownership of the stream feeding this panel.
        void attach(std::unique_ptr<LogStream> stream);
        // Callback to hand to a LogStream; appends and redraws.
        LoggingManager::LogCallback sink();
        void append(std::string line);
        void close();
        void set_title(const std::string &title);

        bool is_open() const;
        void scroll(int delta);
        bool contains(int x, int y) const;
        ftxui::Element render(const std::string &hint = "Alt+c close ");

        // Strips ANSI escape sequences and carriage returns, expands tabs.
        static std::string sanitize(const std::string &line);

    private:
        ftxui::ScreenInteractive &screen_;
        size_t max_lines_;
        std::unique_ptr<LogStream> stream_;
        mutable std::mutex mutex_;
        bool open_ = false;
        std::string title_;
        std::vector<std::string> lines_;
        int scroll_position_ = 0;
        bool follow_tail_ = true;
        ftxui::Box box_;
    };

} // namespace gsystemctl::ui
