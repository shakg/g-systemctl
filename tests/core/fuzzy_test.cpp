#include <gtest/gtest.h>
#include <g-systemctl/core/fuzzy.hpp>

namespace gsystemctl {

using Positions = std::vector<size_t>;

TEST(FuzzyTest, EmptyNeedleMatches) {
    auto m = fuzzy_match("", "nginx");
    ASSERT_TRUE(m);
    EXPECT_TRUE(m->empty());
}

TEST(FuzzyTest, PrefersSubstring) {
    // "ng" as a subsequence starts at 0 ("n", "g"), but the substring is at 3.
    EXPECT_EQ(fuzzy_match("ng", "nodng"), (Positions{3, 4}));
}

TEST(FuzzyTest, Subsequence) {
    EXPECT_EQ(fuzzy_match("ngx", "nginx"), (Positions{0, 1, 4}));
}

TEST(FuzzyTest, CaseInsensitive) {
    EXPECT_EQ(fuzzy_match("SSH", "sshd.service"), (Positions{0, 1, 2}));
}

TEST(FuzzyTest, NoMatch) {
    EXPECT_FALSE(fuzzy_match("xz", "nginx"));
    EXPECT_FALSE(fuzzy_match("toolong", "abc"));
}

TEST(FindAllTest, AllOccurrences) {
    using Ranges = std::vector<std::pair<size_t, size_t>>;
    EXPECT_EQ(find_all("Err", "error: err again"), (Ranges{{0, 3}, {7, 10}}));
    EXPECT_EQ(find_all("aa", "aaaa"), (Ranges{{0, 2}, {2, 4}}));
    EXPECT_TRUE(find_all("", "abc").empty());
    EXPECT_TRUE(find_all("x", "abc").empty());
}

} // namespace gsystemctl
