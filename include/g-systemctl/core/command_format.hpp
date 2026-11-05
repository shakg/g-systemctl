#pragma once

#include <optional>
#include <string>
#include <vector>

// Breaks long command lines into one option per line for the detail pages.
namespace gsystemctl {

struct CommandLine {
    int indent = 0; // in columns
    std::string text;
};

// Splits on whitespace. Quoted parts ('...' or "...") stay in one token, quotes kept.
std::vector<std::string> split_command(const std::string& command);

// `["sh","-c","echo hi"]` -> {sh, -c, echo hi}. nullopt if `text` is not a JSON string array.
std::optional<std::vector<std::string>> parse_json_string_array(const std::string& text);

// Program on the first line, then each option with its value on its own line,
// indented under the program. The script of `sh -c` / `bash -c` is laid out
// the same way, one level deeper. Shell separators (&&, ||, ;, |) start a new
// command. Commands of at most `single_line_max` characters stay on one line.
std::vector<CommandLine> format_command(const std::vector<std::string>& tokens, size_t single_line_max = 60);
std::vector<CommandLine> format_command(const std::string& command, size_t single_line_max = 60);

} // namespace gsystemctl
