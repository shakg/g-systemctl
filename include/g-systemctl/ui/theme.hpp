#pragma once

#include "g-systemctl/core/fuzzy.hpp"
#include "g-systemctl/core/text_wrap.hpp"

#include <algorithm>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>

// Palette and table-cell helpers shared by the services and containers tabs.
namespace gsystemctl::ui::theme
{

    inline ftxui::Color BackgroundColor() { return ftxui::Color::RGB(11, 12, 19); }
    inline ftxui::Color PanelColor() { return ftxui::Color::RGB(16, 17, 27); }
    inline ftxui::Color BorderColor() { return ftxui::Color::RGB(40, 43, 61); }
    inline ftxui::Color MutedColor() { return ftxui::Color::RGB(103, 110, 140); }
    inline ftxui::Color TextColor() { return ftxui::Color::RGB(180, 186, 208); }
    inline ftxui::Color HeaderColor() { return ftxui::Color::RGB(112, 119, 150); }
    inline ftxui::Color AccentColor() { return ftxui::Color::RGB(110, 171, 147); }
    inline ftxui::Color StandaloneColor() { return ftxui::Color::RGB(150, 160, 220); }
    inline ftxui::Color WarningColor() { return ftxui::Color::RGB(255, 190, 45); }

    inline ftxui::Element cell(const std::string &value, int width, ftxui::Color fg = TextColor(), bool bold_text = false)
    {
        using namespace ftxui;
        // Cut with "…" rather than clipped mid-word.
        auto element = text(ellipsize(value, width)) | color(fg);
        if (bold_text)
        {
            element = element | bold;
        }
        return element | size(WIDTH, EQUAL, width);
    }

    inline ftxui::Element right_cell(const std::string &value, int width, ftxui::Color fg = TextColor())
    {
        using namespace ftxui;
        return text(ellipsize(value, width)) | color(fg) | align_right | size(WIDTH, EQUAL, width);
    }

    inline ftxui::Color MatchColor() { return ftxui::Color::RGB(255, 190, 45); }

    // `value` with the bytes at `positions` (sorted, from fuzzy_match) drawn
    // in MatchColor, bold and underlined. Not cut: the caller sizes it.
    inline ftxui::Element highlighted(const std::string &value, const std::vector<size_t> &positions, ftxui::Color fg)
    {
        using namespace ftxui;
        if (positions.empty())
            return text(value) | color(fg);
        Elements parts;
        size_t i = 0, k = 0;
        while (i < value.size())
        {
            const bool hit = k < positions.size() && positions[k] == i;
            size_t j = i;
            if (hit)
                while (k < positions.size() && positions[k] == j)
                    ++k, ++j;
            else
                j = k < positions.size() ? positions[k] : value.size();
            // Keep a UTF-8 sequence whole.
            while (j < value.size() && (static_cast<unsigned char>(value[j]) & 0xC0) == 0x80)
                ++j;
            auto part = text(value.substr(i, j - i));
            parts.push_back(hit ? part | color(MatchColor()) | bold | underlined : part | color(fg));
            i = j;
        }
        return hbox(std::move(parts));
    }

    // Fuzzy highlight of `filter` in `value`.
    inline ftxui::Element filter_highlighted(const std::string &value, const std::string &filter, ftxui::Color fg)
    {
        auto m = fuzzy_match(filter, value);
        return highlighted(value, m ? *m : std::vector<size_t>{}, fg);
    }

    // `filter`: fuzzy matches in `value` are highlighted.
    inline ftxui::Element flex_cell(const std::string &value, ftxui::Color fg = TextColor(), bool bold_text = false,
                                    const std::string &filter = "")
    {
        using namespace ftxui;
        auto element = filter_highlighted(value, filter, fg);
        if (bold_text)
        {
            element = element | bold;
        }
        return element | flex;
    }


    // A dialog drawn over the page. FTXUI merges neighbouring box-drawing
    // cells when both allow it (turning a border's "─" into "┴" where a page
    // line meets it). This clears that flag on the page cells around the
    // dialog, so its border never joins the page's lines while its own
    // separators still join its border ("├───┤").
    class IsolatedBorders : public ftxui::Node
    {
    public:
        explicit IsolatedBorders(ftxui::Element child) : Node({std::move(child)}) {}
        void ComputeRequirement() override
        {
            children_[0]->ComputeRequirement();
            requirement_ = children_[0]->requirement();
        }
        void SetBox(ftxui::Box box) override
        {
            Node::SetBox(box);
            children_[0]->SetBox(box);
        }
        void Render(ftxui::Screen &screen) override
        {
            children_[0]->Render(screen);
            for (int y = std::max(box_.y_min - 1, 0); y <= std::min(box_.y_max + 1, screen.dimy() - 1); ++y)
                for (int x = std::max(box_.x_min - 1, 0); x <= std::min(box_.x_max + 1, screen.dimx() - 1); ++x)
                {
                    const bool inside = y >= box_.y_min && y <= box_.y_max && x >= box_.x_min && x <= box_.x_max;
                    if (!inside)
                        screen.PixelAt(x, y).automerge = false;
                }
        }
    };

    inline ftxui::Element isolated_borders(ftxui::Element dialog)
    {
        return std::make_shared<IsolatedBorders>(std::move(dialog));
    }

} // namespace gsystemctl::ui::theme
