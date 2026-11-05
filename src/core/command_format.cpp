#include "g-systemctl/core/command_format.hpp"
#include <cctype>
#include <cstdlib>
#include <deque>

namespace gsystemctl {

namespace {

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool has_space(const std::string& value) {
    for (char c : value) {
        if (is_space(c)) {
            return true;
        }
    }
    return false;
}

// "-x", "--long", "--key=value"; not "-", "-1", "-.5".
bool is_option(const std::string& token) {
    if (token.size() < 2 || token[0] != '-') {
        return false;
    }
    char next = token[1];
    return !(std::isdigit(static_cast<unsigned char>(next)) || next == '.');
}

bool is_separator(const std::string& token) {
    return token == "&&" || token == "||" || token == ";" || token == "|";
}

bool is_shell(const std::string& program) {
    auto slash = program.find_last_of('/');
    auto name = slash == std::string::npos ? program : program.substr(slash + 1);
    return name == "sh" || name == "bash" || name == "zsh" || name == "dash" || name == "ash";
}

std::string unquote(const std::string& token) {
    if (token.size() >= 2 && (token.front() == '"' || token.front() == '\'') && token.back() == token.front()) {
        return token.substr(1, token.size() - 2);
    }
    return token;
}

// Tokens shown as typed: ones with spaces get quotes back.
std::string display(const std::string& token) {
    if (!has_space(token) || token.front() == '"' || token.front() == '\'') {
        return token;
    }
    return "\"" + token + "\"";
}

} // namespace

std::vector<std::string> split_command(const std::string& command) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_token = false;
    char quote = 0;
    for (char c : command) {
        if (quote) {
            current += c;
            if (c == quote) {
                quote = 0;
            }
        } else if (is_space(c)) {
            if (in_token) {
                tokens.push_back(current);
                current.clear();
                in_token = false;
            }
        } else {
            if (c == '"' || c == '\'') {
                quote = c;
            }
            current += c;
            in_token = true;
        }
    }
    if (in_token) {
        tokens.push_back(current);
    }
    return tokens;
}

std::optional<std::vector<std::string>> parse_json_string_array(const std::string& text) {
    size_t i = 0;
    auto skip_space = [&] {
        while (i < text.size() && is_space(text[i])) {
            ++i;
        }
    };
    skip_space();
    if (i >= text.size() || text[i] != '[') {
        return std::nullopt;
    }
    ++i;
    std::vector<std::string> items;
    skip_space();
    if (i < text.size() && text[i] == ']') {
        ++i;
        skip_space();
        return i == text.size() ? std::optional(items) : std::nullopt;
    }
    while (true) {
        skip_space();
        if (i >= text.size() || text[i] != '"') {
            return std::nullopt;
        }
        ++i;
        std::string item;
        while (i < text.size() && text[i] != '"') {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                switch (text[i]) {
                case 'n': item += '\n'; break;
                case 't': item += '\t'; break;
                case 'u':
                    // Docker escapes <, >, & as < etc.; keep ASCII ones, drop the rest.
                    if (i + 4 < text.size()) {
                        unsigned long code = std::strtoul(text.substr(i + 1, 4).c_str(), nullptr, 16);
                        if (code < 0x80) {
                            item += static_cast<char>(code);
                        }
                        i += 4;
                    }
                    break;
                default: item += text[i]; break;
                }
            } else {
                item += text[i];
            }
            ++i;
        }
        if (i >= text.size()) {
            return std::nullopt;
        }
        ++i; // closing quote
        items.push_back(item);
        skip_space();
        if (i < text.size() && text[i] == ',') {
            ++i;
            continue;
        }
        if (i < text.size() && text[i] == ']') {
            ++i;
            skip_space();
            return i == text.size() ? std::optional(items) : std::nullopt;
        }
        return std::nullopt;
    }
}

std::vector<CommandLine> format_command(const std::vector<std::string>& input, size_t single_line_max) {
    std::string joined;
    for (const auto& token : input) {
        joined += (joined.empty() ? "" : " ") + display(token);
    }
    if (joined.size() <= single_line_max) {
        return joined.empty() ? std::vector<CommandLine>{} : std::vector<CommandLine>{{0, joined}};
    }

    std::deque<std::string> tokens(input.begin(), input.end());
    std::vector<CommandLine> lines;
    int base = 0;              // indent of the current program line
    bool expect_program = true;
    bool option_takes_value = false;
    std::string program;

    while (!tokens.empty()) {
        std::string token = tokens.front();
        tokens.pop_front();

        if (is_separator(token)) {
            lines.push_back({base, token});
            expect_program = true;
            option_takes_value = false;
            continue;
        }
        if (expect_program) {
            // A script passed as one token (`sh -c "a -b && c"`) is laid out like the rest.
            auto inner = unquote(token);
            if (has_space(inner)) {
                auto split = split_command(inner);
                tokens.insert(tokens.begin(), split.begin(), split.end());
                continue;
            }
            bool after_separator = !lines.empty() && is_separator(lines.back().text);
            if (after_separator) {
                lines.back().text += " " + token;
            } else {
                lines.push_back({base, token});
            }
            program = token;
            expect_program = false;
            option_takes_value = false;
            continue;
        }
        if (is_option(token)) {
            if (token == "-c" && is_shell(program)) {
                // `bash -c <script>`: the script is a command of its own, one level in.
                lines.back().text += " " + token;
                base += 2;
                expect_program = true;
                continue;
            }
            lines.push_back({base + 2, token});
            option_takes_value = token.find('=') == std::string::npos;
            continue;
        }
        // Value of the option above, or a positional argument.
        if (option_takes_value) {
            lines.back().text += " " + display(token);
            option_takes_value = false;
        } else if (lines.back().indent == base) {
            // Arguments before the first option stay with the program.
            lines.back().text += " " + display(token);
        } else {
            lines.push_back({base + 2, display(token)});
        }
    }
    return lines;
}

std::vector<CommandLine> format_command(const std::string& command, size_t single_line_max) {
    return format_command(split_command(command), single_line_max);
}

} // namespace gsystemctl
