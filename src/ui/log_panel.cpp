#include "g-systemctl/ui/log_panel.hpp"
#include "g-systemctl/core/fuzzy.hpp"
#include "g-systemctl/core/text_wrap.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    namespace
    {
        // Log lines wrapped to the width this node is given. The width is only
        // known after layout, so the node starts from the width of the previous
        // frame (`width`, kept by LogPanel) and Check() asks FTXUI for another
        // pass only when the real width differs.
        // Selects the last row of `focus_line` so a surrounding frame scrolls to it.
        class WrappedLines : public Node
        {
        public:
            WrappedLines(std::vector<std::string> lines, int focus_line, int &width,
                         std::shared_ptr<LogPanel::Layout> layout, bool selected, TextPoint a, TextPoint b,
                         LogPanel::Pin pin, std::string search, int match_line)
                : lines_(std::move(lines)), focus_line_(focus_line), width_(width), layout_(std::move(layout)),
                  selected_(selected), a_(a), b_(b), pin_(pin), search_(std::move(search)), match_line_(match_line) {}

            void ComputeRequirement() override
            {
                std::vector<std::vector<std::string>> wrapped;
                size_t count = 0;
                for (auto &line : lines_)
                {
                    wrapped.push_back(wrap_text(line, width_));
                    count += wrapped.back().size();
                }
                // Sized before reflect() takes references into it.
                layout_->rows.clear();
                layout_->boxes.assign(count, Box{});

                Elements rows;
                int focus_row = 0;
                for (size_t i = 0; i < wrapped.size(); ++i)
                {
                    int col = 0;
                    size_t offset = 0; // byte offset of the row in its line
                    const int line = static_cast<int>(i);
                    const auto hits = find_all(search_, lines_[i]);
                    for (auto &row : wrapped[i])
                    {
                        const int w = string_width(row);
                        layout_->rows.push_back({line, col, w});
                        rows.push_back(render_row(row, line, col, w, hits, offset) |
                                       reflect(layout_->boxes[layout_->rows.size() - 1]));
                        col += w;
                        offset += row.size();
                    }
                    if (line == focus_line_)
                    {
                        focus_row = static_cast<int>(rows.size()) - 1;
                    }
                }
                layout_->focus_line = focus_line_;
                if (pin_.on)
                {
                    // yframe centers the focused row: focus half a frame below the pinned top row.
                    const auto &info = layout_->rows;
                    int top = 0;
                    for (size_t r = 0; r < info.size(); ++r)
                    {
                        if (info[r].line < pin_.top.line ||
                            (info[r].line == pin_.top.line && info[r].col <= pin_.top.col))
                            top = static_cast<int>(r);
                    }
                    focus_row = std::min(top + pin_.extent / 2, std::max(0, static_cast<int>(info.size()) - 1));
                    if (!info.empty())
                        layout_->focus_line = info[focus_row].line;
                }
                children_ = {vbox(std::move(rows))};
                children_[0]->ComputeRequirement();
                requirement_ = children_[0]->requirement();
                // Width follows the parent, not the content.
                requirement_.min_x = 0;
                requirement_.flex_grow_x = 1;
                requirement_.flex_shrink_x = 1;
                requirement_.selection = Requirement::NORMAL;
                requirement_.selected_box = Box{0, 0, focus_row, focus_row};
            }

            void SetBox(Box box) override
            {
                Node::SetBox(box);
                children_[0]->SetBox(box);
            }

            void Check(Status *status) override
            {
                Node::Check(status);
                // Iteration 0 runs before any layout: box_ is still empty, not a width.
                if (status->iteration == 0)
                    return;
                const int width = box_.x_max - box_.x_min + 1;
                if (width > 0 && width != width_)
                {
                    width_ = width;
                    status->need_iteration = true;
                }
            }

        private:
            // The selected part of a row is drawn inverted; otherwise search hits are highlighted.
            Element render_row(const std::string &row, int line, int col, int w,
                               const std::vector<std::pair<size_t, size_t>> &hits, size_t offset) const
            {
                if (!selected_ || line < a_.line || line > b_.line)
                    return render_hits(row, line, hits, offset);
                const int from = line == a_.line ? a_.col : 0;
                const int to = line == b_.line ? b_.col + 1 : col + w;
                const int s = std::max(from, col) - col;
                const int e = std::min(to, col + w) - col;
                if (s >= e)
                    return text(row);
                return hbox({
                    text(slice_columns(row, 0, s)),
                    text(slice_columns(row, s, e)) | inverted,
                    text(slice_columns(row, e, w)),
                });
            }

            Element render_hits(const std::string &row, int line, const std::vector<std::pair<size_t, size_t>> &hits,
                                size_t offset) const
            {
                Elements parts;
                size_t at = 0; // within row
                const auto hl = line == match_line_ ? AccentColor() : WarningColor();
                for (const auto &[b, e] : hits)
                {
                    if (e <= offset || b >= offset + row.size())
                        continue;
                    const size_t s = std::max(b, offset) - offset;
                    const size_t t = std::min(e, offset + row.size()) - offset;
                    if (s > at)
                        parts.push_back(text(row.substr(at, s - at)));
                    parts.push_back(text(row.substr(s, t - s)) | bgcolor(hl) | color(Color::Black) | bold);
                    at = t;
                }
                if (parts.empty())
                    return text(row);
                if (at < row.size())
                    parts.push_back(text(row.substr(at)));
                return hbox(std::move(parts));
            }

            std::vector<std::string> lines_;
            int focus_line_;
            int &width_;
            std::shared_ptr<LogPanel::Layout> layout_;
            bool selected_;
            TextPoint a_, b_;
            LogPanel::Pin pin_;
            std::string search_;
            int match_line_;
        };
    } // namespace

    LogPanel::LogPanel(ScreenInteractive &screen, size_t max_lines) : screen_(screen), max_lines_(max_lines) {}

    LogPanel::~LogPanel()
    {
        // Join the reader thread before the members it writes to go away.
        stream_.reset();
    }

    void LogPanel::open(const std::string &title, const std::string &first_line)
    {
        // Not under the lock: the stream's reader thread may be blocked in append().
        stream_.reset();
        search_.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        title_ = title;
        lines_.clear();
        if (!first_line.empty())
        {
            lines_.push_back(first_line);
        }
        scroll_position_ = 0;
        follow_tail_ = true;
        has_selection_ = selecting_ = false;
        pin_.on = false;
        match_line_ = -1;
        open_ = true;
    }

    void LogPanel::attach(std::unique_ptr<LogStream> stream)
    {
        stream_ = std::move(stream);
    }

    LoggingManager::LogCallback LogPanel::sink()
    {
        return [this](std::string line) {
            append(std::move(line));
        };
    }

    void LogPanel::append(std::string line)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back(sanitize(line));
            if (lines_.size() > max_lines_)
            {
                const int dropped = static_cast<int>(max_lines_ / 5);
                lines_.erase(lines_.begin(), lines_.begin() + dropped);
                if (match_line_ >= 0)
                    match_line_ = std::max(-1, match_line_ - dropped);
                if (has_selection_)
                {
                    anchor_.line -= dropped;
                    cursor_.line -= dropped;
                    if (anchor_.line < 0 && cursor_.line < 0)
                    {
                        has_selection_ = selecting_ = false;
                    }
                    pin_.top.line -= dropped;
                    for (auto *p : {&anchor_, &cursor_, &pin_.top})
                        if (p->line < 0)
                            *p = TextPoint{0, 0};
                }
            }
            const int last = std::max(0, static_cast<int>(lines_.size()) - 1);
            scroll_position_ = follow_tail_ ? last : std::clamp(scroll_position_, 0, last);
        }
        screen_.PostEvent(Event::Custom);
    }

    void LogPanel::close()
    {
        stream_.reset();
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
        title_.clear();
        lines_.clear();
        scroll_position_ = 0;
        follow_tail_ = true;
        has_selection_ = selecting_ = false;
        pin_.on = false;
        match_line_ = -1;
    }

    void LogPanel::set_title(const std::string &title)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        title_ = title;
    }

    bool LogPanel::is_open() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return open_;
    }

    void LogPanel::scroll(int delta)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_ || lines_.empty())
        {
            return;
        }
        const int max_position = static_cast<int>(lines_.size()) - 1;
        if (pin_.on)
        {
            // Continue from where the pinned view is.
            pin_.on = false;
            follow_tail_ = false;
            scroll_position_ = layout_->focus_line;
        }
        scroll_position_ = std::clamp(scroll_position_ + delta, 0, max_position);
        follow_tail_ = scroll_position_ == max_position;
    }

    void LogPanel::set_search(const std::string &query)
    {
        const bool was_empty = search_.empty();
        search_ = query;
        if (query.empty())
        {
            std::lock_guard<std::mutex> lock(mutex_);
            match_line_ = -1;
            if (!was_empty && !pin_.on)
                follow_tail_ = true;
            return;
        }
        {
            // Look again from the end, so the newest match is shown first.
            std::lock_guard<std::mutex> lock(mutex_);
            match_line_ = static_cast<int>(lines_.size());
        }
        next_match(false);
    }

    void LogPanel::next_match(bool forward)
    {
        if (search_.empty())
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        const int n = static_cast<int>(lines_.size());
        if (n == 0)
            return;
        int from = match_line_ >= 0 ? match_line_ : scroll_position_;
        for (int step = 1; step <= n + 1; ++step)
        {
            const int i = ((from + (forward ? step : -step)) % n + n) % n;
            if (!find_all(search_, lines_[i]).empty())
            {
                match_line_ = i;
                pin_.on = false;
                scroll_position_ = i;
                follow_tail_ = false;
                return;
            }
        }
        match_line_ = -1;
    }

    std::string LogPanel::match_summary() const
    {
        if (search_.empty())
            return "";
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0, current = 0;
        for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
        {
            if (find_all(search_, lines_[i]).empty())
                continue;
            ++total;
            if (i <= match_line_)
                current = total;
        }
        if (total == 0)
            return "no matches";
        return std::to_string(std::max(current, 1)) + "/" + std::to_string(total);
    }

    bool LogPanel::contains(int x, int y) const
    {
        return box_.Contain(x, y);
    }

    Element LogPanel::render(Element actions)
    {
        std::string title;
        std::vector<std::string> lines;
        int scroll_position = 0;
        bool following = true;
        bool selected = false;
        Pin pin;
        TextPoint a, b;
        int match_line = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            title = title_;
            match_line = match_line_;
            const int last = std::max(0, static_cast<int>(lines_.size()) - 1);
            scroll_position_ = follow_tail_ ? last : std::clamp(scroll_position_, 0, last);
            scroll_position = scroll_position_;
            following = follow_tail_;
            lines = lines_;
            selected = has_selection_;
            pin = pin_;
            a = anchor_;
            b = cursor_;
        }
        if (b.line < a.line || (b.line == a.line && b.col < a.col))
            std::swap(a, b);

        Element body = lines.empty() ? text("Waiting for log output...") | dim
                                     : std::make_shared<WrappedLines>(std::move(lines), scroll_position, wrap_width_,
                                                                               layout_, selected, a, b, pin, search_, match_line);

        // Fit the header: the buttons were already shortened by their owner; the
        // state shortens to "paused", then the title ends in "…". Always a gap
        // between title and state.
        actions->ComputeRequirement();
        const int width = box_.x_max - box_.x_min + 1 > 1 ? box_.x_max - box_.x_min + 1 : Terminal::Size().dimx - 2;
        const int fixed = string_width(" LOGS ") + 2 + actions->requirement().min_x;
        std::string state = following ? "following  " : "paused (scroll to end to follow)  ";
        if (!following && fixed + string_width(title) + string_width(state) > width)
            state = "paused  ";
        bool noticed = false;
        if (!notice_.empty() && std::chrono::steady_clock::now() < notice_until_)
        {
            state = notice_ + "  ";
            noticed = true;
            if (fixed + string_width(state) > width)
                state = "copied  ";
        }
        const int title_room = width - fixed - string_width(state);
        auto glyphs = Utf8ToGlyphs(title);
        if (static_cast<int>(glyphs.size()) > title_room)
        {
            title.clear();
            for (int i = 0; i < title_room - 1; ++i)
                title += glyphs[i];
            title += title_room > 0 ? "…" : "";
        }

        return vbox({
                   hbox({
                       text(" LOGS ") | color(HeaderColor()),
                       text(title) | color(WarningColor()),
                       text("  "),
                       filler(),
                       text(state) | color(noticed ? AccentColor() : MutedColor()),
                       std::move(actions),
                   }),
                   separator() | color(BorderColor()),
                   std::move(body) | color(TextColor()) | vscroll_indicator | yframe | reflect(body_box_) | flex,
               }) |
               bgcolor(PanelColor()) | reflect(box_);
    }

    bool LogPanel::point_at(int x, int y, TextPoint &out) const
    {
        const auto &rows = layout_->rows;
        const auto &boxes = layout_->boxes;
        int first = -1, last = -1;
        for (size_t i = 0; i < rows.size() && i < boxes.size(); ++i)
        {
            // reflect() clips boxes to what is drawn: rows scrolled out of view are empty.
            const int row_y = boxes[i].y_min;
            if (boxes[i].y_max < row_y || boxes[i].x_max < boxes[i].x_min || row_y < body_box_.y_min ||
                row_y > body_box_.y_max)
                continue;
            if (first < 0)
                first = static_cast<int>(i);
            last = static_cast<int>(i);
            if (row_y == y)
            {
                const auto &r = rows[i];
                out = {r.line, r.col + std::clamp(x - boxes[i].x_min, 0, std::max(0, r.width - 1))};
                return true;
            }
        }
        if (first < 0)
            return false;
        // Above or below the text (dragging past an edge, or the empty space under short logs).
        if (y < boxes[first].y_min)
            out = {rows[first].line, rows[first].col};
        else
            out = {rows[last].line, rows[last].col + std::max(0, rows[last].width - 1)};
        return true;
    }

    bool LogPanel::handle_mouse(const Mouse &mouse)
    {
        if (!is_open())
            return false;
        const bool left = mouse.button == Mouse::Left;
        if (selecting_)
        {
            TextPoint p;
            const bool released = mouse.motion == Mouse::Released;
            if (point_at(mouse.x, mouse.y, p))
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cursor_ = p;
            }
            if (released)
            {
                selecting_ = false;
                std::string text;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    text = selected_text(lines_, anchor_, cursor_);
                }
                copy_text(text, "selection");
            }
            return true;
        }
        if (!left || mouse.motion != Mouse::Pressed)
            return false;
        TextPoint p;
        if (!body_box_.Contain(mouse.x, mouse.y) || !point_at(mouse.x, mouse.y, p))
        {
            clear_selection();
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_selection_)
            resume_follow_ = follow_tail_;
        // Hold the view still while a selection exists.
        if (!pin_.on)
        {
            TextPoint top;
            // Column 0 of a row is where that row starts in its log line.
            if (point_at(body_box_.x_min, body_box_.y_min, top))
            {
                pin_ = {true, top, body_box_.y_max - body_box_.y_min};
            }
        }
        follow_tail_ = false;
        anchor_ = cursor_ = p;
        has_selection_ = selecting_ = true;
        return true;
    }

    void LogPanel::copy()
    {
        std::string text;
        std::string what;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!open_)
                return;
            if (has_selection_)
            {
                text = selected_text(lines_, anchor_, cursor_);
                what = "selection";
            }
            else
            {
                const size_t n = std::min(kCopyLines, lines_.size());
                text = last_lines(lines_, kCopyLines);
                what = "last " + std::to_string(n) + (n == 1 ? " line" : " lines");
            }
        }
        copy_text(text, what);
    }

    void LogPanel::copy_text(const std::string &text, const std::string &what)
    {
        if (text.empty())
        {
            notice_ = "nothing to copy";
        }
        else
        {
            const std::string via = copy_to_clipboard(text);
            notice_ = via.empty() ? "copy failed" : "copied " + what + " via " + via;
        }
        // Cleared by a later redraw (the tab pollers redraw every 2s).
        notice_until_ = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    }

    bool LogPanel::clear_selection()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_selection_)
            return false;
        has_selection_ = selecting_ = false;
        pin_.on = false;
        if (resume_follow_)
            follow_tail_ = true;
        return true;
    }

    std::string LogPanel::sanitize(const std::string &line)
    {
        std::string out;
        out.reserve(line.size());
        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];
            if (c == '\x1b')
            {
                // CSI: ESC [ params final-byte(0x40-0x7e). Other escapes: drop ESC + next byte.
                if (i + 1 < line.size() && line[i + 1] == '[')
                {
                    i += 2;
                    while (i < line.size() && !(line[i] >= 0x40 && line[i] <= 0x7e))
                    {
                        ++i;
                    }
                }
                else
                {
                    ++i;
                }
                continue;
            }
            if (c == '\r')
            {
                continue;
            }
            if (c == '\t')
            {
                out.append(4 - (out.size() % 4), ' ');
                continue;
            }
            out.push_back(c);
        }
        return out;
    }

} // namespace gsystemctl::ui
