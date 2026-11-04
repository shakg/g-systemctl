#pragma once

#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/terminal.hpp>
#include <string>

// Layout shared by the service and container detail pages: a title bar, a
// summary, and a short log strip that can expand to fill the page.
namespace gsystemctl::ui
{

    // Rows of the log strip when it is not expanded (header and separator
    // included): about a third of the terminal.
    inline int log_strip_height()
    {
        return std::clamp(ftxui::Terminal::Size().dimy / 3, 6, 14);
    }

    inline ftxui::Element field_row(const std::string &label, const std::string &value,
                                    ftxui::Color fg = theme::TextColor())
    {
        using namespace ftxui;
        return hbox({
            text(" " + label) | color(theme::HeaderColor()) | size(WIDTH, EQUAL, 16),
            paragraph(value.empty() ? "-" : value) | color(fg) | flex,
        });
    }

    // `first`: no blank line above it.
    inline ftxui::Element section_title(const std::string &title, bool first = false)
    {
        using namespace ftxui;
        auto heading = text(" " + title) | bold | color(theme::AccentColor());
        return first ? heading : vbox({text(""), heading});
    }

    inline ftxui::Element detail_title_bar(const std::string &name, const std::string &subtitle,
                                           ftxui::Element actions)
    {
        using namespace ftxui;
        return hbox({
                   text(" "),
                   text(name) | bold | color(theme::WarningColor()),
                   text(subtitle.empty() ? "" : "  " + subtitle) | color(theme::MutedColor()),
                   filler(),
                   std::move(actions),
               }) |
               bgcolor(theme::PanelColor());
    }

    // Scrolls the summary one element at a time; it can be taller than the screen.
    class SummaryPane
    {
    public:
        ftxui::Element render(ftxui::Elements lines)
        {
            using namespace ftxui;
            count_ = static_cast<int>(lines.size());
            offset_ = std::clamp(offset_, 0, std::max(0, count_ - 1));
            auto content = vbox(Elements(lines.begin() + offset_, lines.end()));
            content->ComputeRequirement();
            const int needed = content->requirement().min_y;
            // Height from the previous frame; good enough to say whether more is hidden below.
            const int visible = box_.y_max - box_.y_min + 1;
            if (visible > 1 && needed > visible)
            {
                const int hidden = needed - (visible - 1);
                return vbox({
                           content | yframe | flex,
                           text(" ↓ " + std::to_string(hidden) + " more rows (↑↓ or wheel to scroll)") |
                               color(theme::MutedColor()),
                       }) |
                       flex | reflect(box_);
            }
            return content | yframe | flex | reflect(box_);
        }
        void scroll(int delta) { offset_ = std::clamp(offset_ + delta, 0, std::max(0, count_ - 1)); }
        void reset() { offset_ = 0; }
        bool contains(int x, int y) const { return box_.Contain(x, y); }

    private:
        int offset_ = 0;
        int count_ = 0;
        ftxui::Box box_;
    };

    inline ftxui::Element detail_page(ftxui::Element title_bar, ftxui::Element summary, ftxui::Element logs,
                                      bool logs_full)
    {
        using namespace ftxui;
        auto rule = separator() | color(theme::BorderColor());
        if (logs_full)
        {
            return vbox({std::move(title_bar), rule, std::move(logs) | flex}) | flex;
        }
        return vbox({
                   std::move(title_bar),
                   rule,
                   std::move(summary) | flex,
                   separator() | color(theme::BorderColor()),
                   std::move(logs) | size(HEIGHT, EQUAL, log_strip_height()),
               }) |
               flex;
    }

} // namespace gsystemctl::ui
