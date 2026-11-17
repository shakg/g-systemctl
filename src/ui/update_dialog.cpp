#include "g-systemctl/ui/update_dialog.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    void UpdateDialog::offer(const std::string &version, const std::string &current, const std::string &blocker,
                             std::function<void(Choice)> on_choice)
    {
        version_ = version;
        current_ = current;
        blocker_ = blocker;
        on_choice_ = std::move(on_choice);
        result_ = false;
        open_ = true;
        opened_at_ = std::chrono::steady_clock::now();
    }

    void UpdateDialog::show_result(bool ok, const std::string &message)
    {
        ok_ = ok;
        message_ = message;
        on_choice_ = nullptr;
        result_ = true;
        open_ = true;
    }

    void UpdateDialog::choose(Choice choice)
    {
        auto callback = std::move(on_choice_);
        on_choice_ = nullptr;
        open_ = false;
        if (callback)
            callback(choice);
    }

    Component UpdateDialog::component()
    {
        auto renderer = Renderer([this](bool /*focused*/) {
            Elements body;
            if (result_)
            {
                body = {
                    text(ok_ ? " Updated" : " Update failed") | bold |
                        color(ok_ ? Colors::running_fg() : Colors::error_fg()),
                    separator() | color(BorderColor()),
                    paragraph(" " + message_ + " ") | color(TextColor()),
                    separator() | color(BorderColor()),
                    text(" any key closes") | color(MutedColor()),
                };
            }
            else
            {
                body = {
                    text(" Update available") | bold | color(AccentColor()),
                    separator() | color(BorderColor()),
                    paragraph(" g-systemctl " + version_ + " is out (you have " + current_ + "). ") |
                        color(TextColor()),
                };
                if (!blocker_.empty())
                    body.push_back(paragraph(" " + blocker_ + " ") | color(WarningColor()));
                body.push_back(text(""));
                Elements buttons{filler()};
                if (blocker_.empty())
                {
                    buttons.push_back(text(" y  Update ") | bold | color(Color::White) | bgcolor(Colors::running_fg()) |
                                      reflect(update_box_));
                    buttons.push_back(text("   "));
                }
                buttons.push_back(text(" n  Not now ") | color(TextColor()) | bgcolor(BorderColor()) |
                                  reflect(later_box_));
                buttons.push_back(text(" "));
                body.push_back(hbox(std::move(buttons)));
                body.push_back(separator() | color(BorderColor()));
                body.push_back(text(std::string(blocker_.empty() ? " y update · " : " ") +
                                    "n / Esc not now · s skip this version · d stop checking ") |
                               color(MutedColor()));
            }
            return vbox(std::move(body)) | size(WIDTH, GREATER_THAN, 50) | size(WIDTH, LESS_THAN, 80) |
                   borderStyled(ROUNDED, BorderColor()) | bgcolor(PanelColor()) | isolated_borders;
        });

        return CatchEvent(renderer, [this](Event event) {
            if (result_)
            {
                if (event.is_character() || event == Event::Return || event == Event::Escape ||
                    (event.is_mouse() && event.mouse().motion == Mouse::Pressed))
                    open_ = false;
                return true;
            }
            if (std::chrono::steady_clock::now() - opened_at_ < kGrace)
                return true;
            const bool click = event.is_mouse() && event.mouse().button == Mouse::Left &&
                               event.mouse().motion == Mouse::Pressed;
            if (event == Event::Character('y') || event == Event::Character('Y') ||
                (click && blocker_.empty() && update_box_.Contain(event.mouse().x, event.mouse().y)))
            {
                if (blocker_.empty())
                    choose(Choice::Update);
                return true;
            }
            if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape ||
                (click && later_box_.Contain(event.mouse().x, event.mouse().y)))
            {
                choose(Choice::NotNow);
                return true;
            }
            if (event == Event::Character('s') || event == Event::Character('S'))
            {
                choose(Choice::Skip);
                return true;
            }
            if (event == Event::Character('d') || event == Event::Character('D'))
            {
                choose(Choice::StopChecking);
                return true;
            }
            // Swallow everything else, Enter included.
            return true;
        });
    }

} // namespace gsystemctl::ui
