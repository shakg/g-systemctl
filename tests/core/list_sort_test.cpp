#include <gtest/gtest.h>
#include <g-systemctl/core/list_sort.hpp>
#include <algorithm>
#include <vector>

namespace gsystemctl {

TEST(ListSortTest, ParsePercent) {
    EXPECT_DOUBLE_EQ(parse_percent("13.40%"), 13.4);
    EXPECT_DOUBLE_EQ(parse_percent("0.0%"), 0.0);
    EXPECT_DOUBLE_EQ(parse_percent("-"), -1);
    EXPECT_DOUBLE_EQ(parse_percent(""), -1);
}

TEST(ListSortTest, ParseSize) {
    EXPECT_DOUBLE_EQ(parse_size("214MiB / 1GiB"), 214.0 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(parse_size("18.9MB"), 18.9 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(parse_size("908.0 KiB"), 908.0 * 1024);
    EXPECT_DOUBLE_EQ(parse_size("1.5GiB / 2GiB"), 1.5 * 1024 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(parse_size("512B"), 512);
    EXPECT_DOUBLE_EQ(parse_size("-"), -1);
}

TEST(ListSortTest, NumbersHighFirstWhenDescending) {
    std::vector<SortValue> v = {{1, "", false, "a"}, {30, "", false, "b"}, {7, "", false, "c"}};
    std::sort(v.begin(), v.end(), [](auto &x, auto &y) { return sort_before(x, y, true); });
    EXPECT_EQ(v[0].name, "b");
    EXPECT_EQ(v[1].name, "c");
    EXPECT_EQ(v[2].name, "a");
}

TEST(ListSortTest, MissingValuesLastInBothDirections) {
    std::vector<SortValue> v = {{0, "", true, "none"}, {5, "", false, "five"}, {1, "", false, "one"}};
    for (bool desc : {false, true}) {
        std::sort(v.begin(), v.end(), [desc](auto &x, auto &y) { return sort_before(x, y, desc); });
        EXPECT_EQ(v.back().name, "none") << desc;
    }
}

TEST(ListSortTest, TextIgnoresCaseAndTiesUseName) {
    std::vector<SortValue> v = {{0, "redis", false, "b"}, {0, "Nginx", false, "z"}, {0, "redis", false, "a"}};
    std::sort(v.begin(), v.end(), [](auto &x, auto &y) { return sort_before(x, y, false); });
    EXPECT_EQ(v[0].name, "z");
    EXPECT_EQ(v[1].name, "a");
    EXPECT_EQ(v[2].name, "b");
}

TEST(ListSortTest, DefaultDirection) {
    EXPECT_FALSE(default_descending(SortColumn::Name));
    EXPECT_TRUE(default_descending(SortColumn::Cpu));
    EXPECT_TRUE(default_descending(SortColumn::Memory));
}

} // namespace gsystemctl
