#pragma once

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
        auto element = text(value) | color(fg);
        if (bold_text)
        {
            element = element | bold;
        }
        return element | size(WIDTH, EQUAL, width);
    }

    inline ftxui::Element right_cell(const std::string &value, int width, ftxui::Color fg = TextColor())
    {
        using namespace ftxui;
        return text(value) | color(fg) | align_right | size(WIDTH, EQUAL, width);
    }

    inline ftxui::Element flex_cell(const std::string &value, ftxui::Color fg = TextColor(), bool bold_text = false)
    {
        using namespace ftxui;
        auto element = text(value) | color(fg);
        if (bold_text)
        {
            element = element | bold;
        }
        return element | flex;
    }

} // namespace gsystemctl::ui::theme
