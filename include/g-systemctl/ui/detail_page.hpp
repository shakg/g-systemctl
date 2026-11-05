#pragma once

#include "g-systemctl/core/command_format.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>
#include <memory>
#include <string>
#include <vector>

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

    // A command laid out one option per line (see format_command). Long
    // lines still wrap, under their own indent.
    inline ftxui::Element command_row(const std::string &label, const std::vector<std::string> &tokens)
    {
        using namespace ftxui;
        auto lines = format_command(tokens);
        if (lines.empty())
        {
            return field_row(label, "");
        }
        Elements rows;
        for (const auto &line : lines)
        {
            rows.push_back(hbox({
                text(std::string(static_cast<size_t>(line.indent), ' ')),
                paragraph(line.text) | color(theme::TextColor()) | flex,
            }));
        }
        return hbox({
            text(" " + label) | color(theme::HeaderColor()) | size(WIDTH, EQUAL, 16),
            vbox(std::move(rows)) | flex,
        });
    }

    inline ftxui::Element command_row(const std::string &label, const std::string &command)
    {
        return command_row(label, split_command(command));
    }

    // `first`: no blank line above it.
    inline ftxui::Element section_title(const std::string &title, bool first = false)
    {
        using namespace ftxui;
        auto heading = text(" " + title) | bold | color(theme::AccentColor());
        return first ? heading : vbox({text(""), heading});
    }

    // Name and subtitle of the title bar. Shrinks below its natural width so
    // the action buttons always fit: the subtitle gives up space first, then
    // the name. Cut text ends with "…".
    class TitleText : public ftxui::Node
    {
    public:
        TitleText(std::string name, std::string subtitle)
            : name_(ftxui::Utf8ToGlyphs(name)), subtitle_(ftxui::Utf8ToGlyphs(subtitle))
        {
        }

        void ComputeRequirement() override
        {
            requirement_.min_x = static_cast<int>(name_.size() + subtitle_.size());
            requirement_.min_y = 1;
            requirement_.flex_shrink_x = 1;
        }

        void Render(ftxui::Screen &screen) override
        {
            const int width = box_.x_max - box_.x_min + 1;
            if (width <= 0 || box_.y_min > box_.y_max)
                return;
            const int name_width = std::min(width, static_cast<int>(name_.size()));
            const int x = draw(screen, box_.x_min, name_, name_width, true, theme::WarningColor());
            draw(screen, x, subtitle_, width - name_width, false, theme::MutedColor());
        }

    private:
        std::vector<std::string> name_;
        std::vector<std::string> subtitle_;

        // Draws `glyphs` into `width` cells, ending with "…" if cut. Returns the next x.
        int draw(ftxui::Screen &screen, int x, const std::vector<std::string> &glyphs, int width, bool bold,
                 ftxui::Color fg) const
        {
            const int count = static_cast<int>(glyphs.size());
            for (int i = 0; i < width && i < count; ++i, ++x)
            {
                auto &pixel = screen.PixelAt(x, box_.y_min);
                pixel.character = (width < count && i == width - 1) ? "…" : glyphs[i];
                pixel.bold = bold;
                pixel.foreground_color = fg;
            }
            return x;
        }
    };

    inline ftxui::Element detail_title_bar(const std::string &name, const std::string &subtitle,
                                           ftxui::Element actions)
    {
        using namespace ftxui;
        return hbox({
                   text(" "),
                   std::make_shared<TitleText>(name, subtitle.empty() ? "" : "  " + subtitle),
                   filler(),
                   text(" "),
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

    // `graphs`, if set, takes the right half of the summary area.
    inline ftxui::Element detail_page(ftxui::Element title_bar, ftxui::Element summary, ftxui::Element logs,
                                      bool logs_full, ftxui::Element graphs = nullptr)
    {
        using namespace ftxui;
        auto rule = separator() | color(theme::BorderColor());
        if (logs_full)
        {
            return vbox({std::move(title_bar), rule, std::move(logs) | flex}) | flex;
        }
        if (graphs)
        {
            summary = hbox({
                std::move(summary) | flex,
                separator() | color(theme::BorderColor()),
                std::move(graphs) | size(WIDTH, EQUAL, ftxui::Terminal::Size().dimx / 2),
            });
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
