#include "g-systemctl/core/fuzzy.hpp"

#include <cctype>

namespace gsystemctl
{
    namespace
    {
        char fold(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

        size_t find_folded(const std::string &needle, const std::string &haystack, size_t from)
        {
            if (needle.size() > haystack.size())
                return std::string::npos;
            for (size_t i = from; i + needle.size() <= haystack.size(); ++i)
            {
                size_t k = 0;
                while (k < needle.size() && fold(haystack[i + k]) == fold(needle[k]))
                    ++k;
                if (k == needle.size())
                    return i;
            }
            return std::string::npos;
        }
    } // namespace

    std::optional<std::vector<size_t>> fuzzy_match(const std::string &needle, const std::string &haystack)
    {
        std::vector<size_t> positions;
        if (needle.empty())
            return positions;
        if (auto at = find_folded(needle, haystack, 0); at != std::string::npos)
        {
            for (size_t i = 0; i < needle.size(); ++i)
                positions.push_back(at + i);
            return positions;
        }
        size_t k = 0;
        for (size_t i = 0; i < haystack.size() && k < needle.size(); ++i)
        {
            if (fold(haystack[i]) == fold(needle[k]))
            {
                positions.push_back(i);
                ++k;
            }
        }
        if (k < needle.size())
            return std::nullopt;
        return positions;
    }

    std::vector<std::pair<size_t, size_t>> find_all(const std::string &needle, const std::string &haystack)
    {
        std::vector<std::pair<size_t, size_t>> out;
        if (needle.empty())
            return out;
        for (size_t at = find_folded(needle, haystack, 0); at != std::string::npos;
             at = find_folded(needle, haystack, at + needle.size()))
            out.emplace_back(at, at + needle.size());
        return out;
    }

} // namespace gsystemctl
