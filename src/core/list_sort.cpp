#include "g-systemctl/core/list_sort.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gsystemctl
{

    namespace
    {
        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        }

        // Leading number of `value` (after spaces); sets `end` past it. False if none.
        bool leading_number(const std::string &value, double &number, size_t &end)
        {
            size_t start = value.find_first_not_of(' ');
            if (start == std::string::npos)
                return false;
            const char *begin = value.c_str() + start;
            char *stop = nullptr;
            number = std::strtod(begin, &stop);
            if (stop == begin)
                return false;
            end = start + static_cast<size_t>(stop - begin);
            return true;
        }
    } // namespace

    bool sort_before(const SortValue &a, const SortValue &b, bool descending)
    {
        if (a.missing != b.missing)
            return !a.missing;
        if (!a.missing)
        {
            if (a.text.empty() && b.text.empty())
            {
                if (a.number != b.number)
                    return descending ? a.number > b.number : a.number < b.number;
            }
            else
            {
                const auto x = lower(a.text), y = lower(b.text);
                if (x != y)
                    return descending ? x > y : x < y;
            }
        }
        return lower(a.name) < lower(b.name);
    }

    double parse_percent(const std::string &value)
    {
        double number = 0;
        size_t end = 0;
        return leading_number(value, number, end) ? number : -1;
    }

    double parse_size(const std::string &value)
    {
        double number = 0;
        size_t end = 0;
        if (!leading_number(value, number, end))
            return -1;
        while (end < value.size() && value[end] == ' ')
            ++end;
        const auto unit = lower(value.substr(end, 3));
        const char first = unit.empty() ? 'b' : unit[0];
        switch (first)
        {
        case 'k':
            return number * 1024.0;
        case 'm':
            return number * 1024.0 * 1024.0;
        case 'g':
            return number * 1024.0 * 1024.0 * 1024.0;
        case 't':
            return number * 1024.0 * 1024.0 * 1024.0 * 1024.0;
        default:
            return number;
        }
    }

    bool default_descending(SortColumn column)
    {
        switch (column)
        {
        case SortColumn::Pid:
        case SortColumn::Cpu:
        case SortColumn::Memory:
        case SortColumn::Pids:
            return true;
        default:
            return false;
        }
    }

} // namespace gsystemctl
