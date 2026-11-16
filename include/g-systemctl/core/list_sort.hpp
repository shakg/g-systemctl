#pragma once

#include <string>

namespace gsystemctl
{

    // Sort order of a list (services, containers). Which columns exist
    // depends on the list; Name is always first.
    enum class SortColumn
    {
        Name,
        Image,
        State,
        Pid,
        Cpu,
        Memory,
        Pids,
    };

    struct SortOrder
    {
        SortColumn column = SortColumn::Name;
        bool descending = false;
    };

    // What a row sorts by in the current column. A row without a value
    // (`missing`, e.g. no CPU sample yet) sorts last in either direction;
    // equal values fall back to `name`, A-Z.
    struct SortValue
    {
        double number = 0;
        std::string text;
        bool missing = false;
        std::string name;
    };

    // True if `a` goes before `b`.
    bool sort_before(const SortValue &a, const SortValue &b, bool descending);

    // "13.40%" -> 13.4. Empty, "-" or unparsable: -1.
    double parse_percent(const std::string &value);

    // Leading size in bytes: "214MiB / 1GiB" -> 214 MiB, "18.9MB", "908.0 KiB",
    // "1.2kB", "512B". MB/GB etc. are read as MiB/GiB (ps and docker mean
    // binary units). Empty, "-" or unparsable: -1.
    double parse_size(const std::string &value);

    // Numbers sort high-first by default (busiest at the top); text A-Z.
    bool default_descending(SortColumn column);

} // namespace gsystemctl
