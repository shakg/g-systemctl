#pragma once

#include "g-systemctl/core/clipboard.hpp"
#include "g-systemctl/core/logging_manager.hpp"
#include <chrono>
#include <ftxui/component/mouse.hpp>
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
        // `actions` is drawn at the right end of the header.
        ftxui::Element render(ftxui::Element actions);

        // Lines Alt+y copies when nothing is selected.
        static constexpr size_t kCopyLines = 100;

        // Mouse drag over the log text selects it; releasing copies it.
        // Returns true when the event was used. A click elsewhere clears the selection.
        bool handle_mouse(const ftxui::Mouse &mouse);
        // Copies the selection, or the last kCopyLines lines when none.
        void copy();
        // Returns false when there was no selection.
        bool clear_selection();

        // Row layout of the last frame: which log line and column each row starts at.
        struct Row
        {
            int line = 0;
            int col = 0;
            int width = 0;
        };
        struct Layout
        {
            std::vector<Row> rows;
            std::vector<ftxui::Box> boxes;
            int focus_line = 0; // log line the frame was focused on
        };
        // Keeps `top` as the first visible row (frame height - 1 = `extent`).
        struct Pin
        {
            bool on = false;
            TextPoint top;
            int extent = 0;
        };

        // Search: matches (case-insensitive) are highlighted; setting it
        // scrolls to the newest matching line. Empty clears it. UI thread.
        void set_search(const std::string &query);
        const std::string &search() const { return search_; }
        // Scrolls to the next (`forward`, newer) or previous matching line, wrapping.
        void next_match(bool forward);
        // "3/12" (current matching line / matching lines), "no matches", or "" with no search.
        std::string match_summary() const;

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
        int wrap_width_ = 0; // width the lines were last wrapped to; UI thread only

        // Selection; line indexes shift when old lines are dropped. UI thread
        // except for the shift in append(), under mutex_.
        bool selecting_ = false; // mouse button held
        bool has_selection_ = false;
        bool resume_follow_ = false; // follow the tail again when the selection is cleared
        TextPoint anchor_, cursor_;
        Pin pin_; // while a selection exists, so new lines do not move the text under it
        std::shared_ptr<Layout> layout_ = std::make_shared<Layout>();
        ftxui::Box body_box_;
        std::string search_;
        int match_line_ = -1; // line the search last moved to; under mutex_
        std::string notice_;
        std::chrono::steady_clock::time_point notice_until_;

        bool point_at(int x, int y, TextPoint &out) const;
        void copy_text(const std::string &text, const std::string &what);

    };

} // namespace gsystemctl::ui
