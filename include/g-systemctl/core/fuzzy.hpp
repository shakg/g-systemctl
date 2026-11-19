#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gsystemctl
{

    // Case-insensitive (ASCII) fuzzy match: every byte of `needle` appears in
    // `haystack` in order. Returns the matched byte positions, preferring a
    // contiguous run (substring) when there is one. An empty needle matches
    // with no positions; no match returns nullopt.
    std::optional<std::vector<size_t>> fuzzy_match(const std::string &needle, const std::string &haystack);

    // Case-insensitive (ASCII) substring occurrences of `needle` in
    // `haystack`, as [begin, end) byte ranges, left to right, not overlapping.
    std::vector<std::pair<size_t, size_t>> find_all(const std::string &needle, const std::string &haystack);

} // namespace gsystemctl
