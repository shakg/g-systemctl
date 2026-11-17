#include "g-systemctl/core/text_wrap.hpp"
#include <algorithm>
#include <ftxui/screen/string.hpp>

namespace gsystemctl
{

    std::vector<std::string> wrap_text(const std::string &line, int width)
    {
        if (width <= 0)
        {
            return {line};
        }
        std::vector<std::string> rows(1);
        int row_width = 0;
        for (const auto &glyph : ftxui::Utf8ToGlyphs(line))
        {
            const int w = glyph.empty() ? 0 : std::max(0, ftxui::string_width(glyph));
            if (row_width > 0 && row_width + w > width)
            {
                rows.emplace_back();
                row_width = 0;
            }
            rows.back() += glyph;
            row_width += w;
        }
        return rows;
    }

    std::vector<std::string> wrap_words(const std::string &text, int width)
    {
        if (width <= 0)
            return {text};
        std::vector<std::string> rows(1);
        int row_width = 0;
        size_t pos = 0;
        while (pos < text.size())
        {
            if (text[pos] == ' ')
            {
                ++pos;
                continue;
            }
            const size_t end = std::min(text.find(' ', pos), text.size());
            const std::string word = text.substr(pos, end - pos);
            pos = end;
            const int w = ftxui::string_width(word);
            if (row_width > 0 && row_width + 1 + w <= width)
            {
                rows.back() += " " + word;
                row_width += 1 + w;
                continue;
            }
            if (row_width > 0)
            {
                rows.emplace_back();
                row_width = 0;
            }
            // Too long for any row: break it, and continue after its last piece.
            auto pieces = wrap_text(word, width);
            for (size_t i = 0; i < pieces.size(); ++i)
            {
                if (i > 0)
                    rows.emplace_back();
                rows.back() = pieces[i];
            }
            row_width = ftxui::string_width(rows.back());
        }
        return rows;
    }

    std::string ellipsize(const std::string &text, int width)
    {
        if (width <= 0)
            return "";
        if (ftxui::string_width(text) <= width)
            return text;
        std::string out;
        int used = 0;
        for (const auto &glyph : ftxui::Utf8ToGlyphs(text))
        {
            const int w = glyph.empty() ? 0 : std::max(0, ftxui::string_width(glyph));
            if (used + w > width - 1)
                break;
            out += glyph;
            used += w;
        }
        return out + "…";
    }

} // namespace gsystemctl
