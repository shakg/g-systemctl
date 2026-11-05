#include <gtest/gtest.h>
#include <g-systemctl/core/command_format.hpp>
#include <string>
#include <vector>

namespace gsystemctl {

namespace {

std::vector<std::string> render(const std::vector<CommandLine>& lines) {
    std::vector<std::string> out;
    for (const auto& line : lines) {
        out.push_back(std::string(static_cast<size_t>(line.indent), ' ') + line.text);
    }
    return out;
}

} // namespace

TEST(CommandFormatTest, ShortCommandStaysOnOneLine) {
    EXPECT_EQ(render(format_command("/usr/sbin/nginx -g daemon on;")),
              std::vector<std::string>{"/usr/sbin/nginx -g daemon on;"});
}

TEST(CommandFormatTest, OneOptionPerLine) {
    auto lines = render(format_command(
        "/usr/bin/llama-server -m /models/qwen.gguf --flash-attn on --temp 0.7 --jinja --min-p 0.0 --port 8080"));
    std::vector<std::string> expected = {
        "/usr/bin/llama-server",
        "  -m /models/qwen.gguf",
        "  --flash-attn on",
        "  --temp 0.7",
        "  --jinja",
        "  --min-p 0.0",
        "  --port 8080",
    };
    EXPECT_EQ(lines, expected);
}

TEST(CommandFormatTest, ShellScriptIsIndentedUnderShell) {
    // systemd joins argv with spaces, so the -c script arrives unquoted.
    auto lines = render(format_command(
        "/usr/bin/bash -c ./llama-server    -m /home/kks/model.gguf    --ctx-size 98000    --host 0.0.0.0"));
    std::vector<std::string> expected = {
        "/usr/bin/bash -c",
        "  ./llama-server",
        "    -m /home/kks/model.gguf",
        "    --ctx-size 98000",
        "    --host 0.0.0.0",
    };
    EXPECT_EQ(lines, expected);
}

TEST(CommandFormatTest, QuotedScriptAndSeparators) {
    std::vector<std::string> tokens = {"sh", "-c", "cd /srv/app && exec python3 server.py --workers 4 --bind 0.0.0.0:80"};
    auto lines = render(format_command(tokens));
    std::vector<std::string> expected = {
        "sh -c",
        "  cd /srv/app",
        "  && exec python3 server.py",
        "    --workers 4",
        "    --bind 0.0.0.0:80",
    };
    EXPECT_EQ(lines, expected);
}

TEST(CommandFormatTest, NegativeNumbersAreValuesAndEqualsOptionsTakeNoValue) {
    auto lines = render(format_command("/opt/bin/worker --offset -5 --mode=fast input.txt output.txt --verbose", 20));
    std::vector<std::string> expected = {
        "/opt/bin/worker",
        "  --offset -5",
        "  --mode=fast",
        "  input.txt",
        "  output.txt",
        "  --verbose",
    };
    EXPECT_EQ(lines, expected);
}

TEST(CommandFormatTest, SplitKeepsQuotedParts) {
    EXPECT_EQ(split_command("a  'b c'  \"d e\" f"), (std::vector<std::string>{"a", "'b c'", "\"d e\"", "f"}));
}

TEST(CommandFormatTest, ParsesJsonStringArrays) {
    EXPECT_EQ(parse_json_string_array(R"(["sh","-c","echo \"hi\" && true"])"),
              (std::vector<std::string>{"sh", "-c", "echo \"hi\" && true"}));
    EXPECT_EQ(parse_json_string_array("[]"), std::vector<std::string>{});
    EXPECT_FALSE(parse_json_string_array("nginx -g"));
    EXPECT_FALSE(parse_json_string_array(R"(["unterminated)"));
}

} // namespace gsystemctl
