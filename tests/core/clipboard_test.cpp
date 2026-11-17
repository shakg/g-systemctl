#include "g-systemctl/core/clipboard.hpp"
#include <gtest/gtest.h>

using namespace gsystemctl;

TEST(Clipboard, Base64)
{
    EXPECT_EQ(base64_encode(""), "");
    EXPECT_EQ(base64_encode("f"), "Zg==");
    EXPECT_EQ(base64_encode("fo"), "Zm8=");
    EXPECT_EQ(base64_encode("foo"), "Zm9v");
    EXPECT_EQ(base64_encode("foobar"), "Zm9vYmFy");
    EXPECT_EQ(base64_encode("a\nb"), "YQpi");
}

TEST(Clipboard, Osc52)
{
    EXPECT_EQ(osc52_sequence("foo", false), "\x1b]52;c;Zm9v\x07");
    EXPECT_EQ(osc52_sequence("foo", true), "\x1bPtmux;\x1b\x1b]52;c;Zm9v\x07\x1b\\");
}

TEST(Clipboard, SliceColumns)
{
    EXPECT_EQ(slice_columns("hello", 1, 3), "el");
    EXPECT_EQ(slice_columns("hello", 3, 100), "lo");
    EXPECT_EQ(slice_columns("a日b", 1, 3), "日");
    EXPECT_EQ(slice_columns("a日b", 3, 4), "b");
}

TEST(Clipboard, SelectedText)
{
    std::vector<std::string> lines = {"first line", "second", "third line"};
    EXPECT_EQ(selected_text(lines, {0, 6}, {0, 9}), "line");
    // Order does not matter; whole middle lines, newline-joined.
    EXPECT_EQ(selected_text(lines, {2, 4}, {0, 6}), "line\nsecond\nthird");
    // Columns past the end of a line take the rest of it.
    EXPECT_EQ(selected_text(lines, {1, 0}, {1, 50}), "second");
    EXPECT_EQ(selected_text({}, {0, 0}, {0, 0}), "");
}

TEST(Clipboard, LastLines)
{
    std::vector<std::string> lines = {"a", "b", "c"};
    EXPECT_EQ(last_lines(lines, 2), "b\nc");
    EXPECT_EQ(last_lines(lines, 10), "a\nb\nc");
    EXPECT_EQ(last_lines({}, 5), "");
}
