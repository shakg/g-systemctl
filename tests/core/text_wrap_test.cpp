#include <gtest/gtest.h>
#include <g-systemctl/core/text_wrap.hpp>
#include <string>
#include <vector>

namespace gsystemctl {

using Rows = std::vector<std::string>;

TEST(TextWrapTest, ShortLineIsOneRow) {
    EXPECT_EQ(wrap_text("hello", 10), Rows{"hello"});
}

TEST(TextWrapTest, ExactWidthIsOneRow) {
    EXPECT_EQ(wrap_text("abcde", 5), Rows{"abcde"});
}

TEST(TextWrapTest, LongLineBreaksAtWidth) {
    EXPECT_EQ(wrap_text("abcdefghij12", 5), (Rows{"abcde", "fghij", "12"}));
}

TEST(TextWrapTest, KeepsSpaces) {
    EXPECT_EQ(wrap_text("  ab  cd", 4), (Rows{"  ab", "  cd"}));
}

TEST(TextWrapTest, EmptyLineIsOneEmptyRow) {
    EXPECT_EQ(wrap_text("", 5), Rows{""});
}

TEST(TextWrapTest, NoWidthLeavesLineAlone) {
    EXPECT_EQ(wrap_text("abcdef", 0), Rows{"abcdef"});
}

TEST(TextWrapTest, MultibyteGlyphsCountAsOneColumn) {
    EXPECT_EQ(wrap_text("äöüßé", 2), (Rows{"äö", "üß", "é"}));
}

TEST(TextWrapTest, WideGlyphIsNotSplit) {
    // Each CJK glyph is two columns wide.
    EXPECT_EQ(wrap_text("a日本", 3), (Rows{"a日", "本"}));
}

} // namespace gsystemctl

namespace gsystemctl {

TEST(WrapWordsTest, BreaksAtSpaces) {
    EXPECT_EQ(wrap_words("one two three four", 9), (Rows{"one two", "three", "four"}));
}

TEST(WrapWordsTest, LongWordIsBrokenNotClipped) {
    EXPECT_EQ(wrap_words("--data-dir /var/lib/app/data/long", 12),
              (Rows{"--data-dir", "/var/lib/app", "/data/long"}));
}

TEST(WrapWordsTest, ShortTextIsOneRow) {
    EXPECT_EQ(wrap_words("ok", 10), Rows{"ok"});
}

TEST(WrapWordsTest, ContinuesAfterBrokenWord) {
    EXPECT_EQ(wrap_words("abcdefgh x", 5), (Rows{"abcde", "fgh x"}));
}

TEST(WrapWordsTest, ZeroWidthReturnsInput) {
    EXPECT_EQ(wrap_words("a b", 0), Rows{"a b"});
}

TEST(EllipsizeTest, ShortTextUnchanged) {
    EXPECT_EQ(ellipsize("Up 3 hours", 22), "Up 3 hours");
}

TEST(EllipsizeTest, CutTextEndsWithEllipsis) {
    EXPECT_EQ(ellipsize("Exited (137) 8 seconds ago", 22), "Exited (137) 8 second…");
}

TEST(EllipsizeTest, WidthOneIsJustEllipsis) {
    EXPECT_EQ(ellipsize("abc", 1), "…");
}

} // namespace gsystemctl
