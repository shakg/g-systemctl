#include "g-systemctl/ui/log_panel.hpp"
#include "g-systemctl/core/text_wrap.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    namespace
    {
        // Log lines wrapped to the width this node is given. The width is only
        // known after layout, so the first pass lays out unwrapped and Check()
        // asks FTXUI for another pass once the width is known or changes.
        // Selects the last row of `focus_line` so a surrounding frame scrolls to it.
        class WrappedLines : public Node
        {
        public:
            WrappedLines(std::vector<std::string> lines, int focus_line)
                : lines_(std::move(lines)), focus_line_(focus_line) {}

            void ComputeRequirement() override
            {
                Elements rows;
                int focus_row = 0;
                for (size_t i = 0; i < lines_.size(); ++i)
                {
                    for (auto &row : wrap_text(lines_[i], width_))
                    {
                        rows.push_back(text(std::move(row)));
                    }
                    if (static_cast<int>(i) == focus_line_)
                    {
                        focus_row = static_cast<int>(rows.size()) - 1;
                    }
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
                const int width = box_.x_max - box_.x_min + 1;
                if (width > 0 && width != width_)
                {
                    width_ = width;
                    status->need_iteration = true;
                }
            }

        private:
            std::vector<std::string> lines_;
            int focus_line_;
            int width_ = 0;
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
        std::lock_guard<std::mutex> lock(mutex_);
        title_ = title;
        lines_.clear();
        if (!first_line.empty())
        {
            lines_.push_back(first_line);
        }
        scroll_position_ = 0;
        follow_tail_ = true;
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
                lines_.erase(lines_.begin(), lines_.begin() + static_cast<long>(max_lines_ / 5));
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
        scroll_position_ = std::clamp(scroll_position_ + delta, 0, max_position);
        follow_tail_ = scroll_position_ == max_position;
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            title = title_;
            const int last = std::max(0, static_cast<int>(lines_.size()) - 1);
            scroll_position_ = follow_tail_ ? last : std::clamp(scroll_position_, 0, last);
            scroll_position = scroll_position_;
            following = follow_tail_;
            lines = lines_;
        }

        Element body = lines.empty() ? text("Waiting for log output...") | dim
                                     : std::make_shared<WrappedLines>(std::move(lines), scroll_position);

        return vbox({
                   hbox({
                       text(" LOGS ") | color(HeaderColor()),
                       text(title) | color(WarningColor()),
                       filler(),
                       text(following ? "following  " : "paused (scroll to end to follow)  ") | color(MutedColor()),
                       std::move(actions),
                   }),
                   separator() | color(BorderColor()),
                   std::move(body) | color(TextColor()) | vscroll_indicator | yframe | flex,
               }) |
               bgcolor(PanelColor()) | reflect(box_);
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
