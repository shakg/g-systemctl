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

} // namespace gsystemctl
