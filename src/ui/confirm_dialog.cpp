#include "g-systemctl/ui/confirm_dialog.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"

using namespace ftxui;

namespace gsystemctl::ui
{
    using namespace theme;

    void ConfirmDialog::ask(const std::string &question, const std::string &confirm_label,
                            std::function<void()> on_confirm)
    {
        question_ = question;
        confirm_label_ = confirm_label;
        on_confirm_ = std::move(on_confirm);
        open_ = true;
    }

    void ConfirmDialog::confirm()
    {
        auto action = std::move(on_confirm_);
        cancel();
        if (action)
            action();
    }

    void ConfirmDialog::cancel()
    {
        open_ = false;
        on_confirm_ = nullptr;
        question_.clear();
        confirm_label_.clear();
    }

    Component ConfirmDialog::component()
    {
        // The bool overload makes the renderer focusable; Modal only routes keys to focusable children.
        auto renderer = Renderer([this](bool /*focused*/) {
            return vbox({
                       text(" Confirm") | bold | color(WarningColor()),
                       separator() | color(BorderColor()),
                       paragraph(" " + question_ + " ") | color(TextColor()),
                       text(""),
                       hbox({
                           filler(),
                           text(" y  " + confirm_label_ + " ") | bold | color(Color::White) |
                               bgcolor(Colors::error_fg()) | reflect(confirm_box_),
                           text("   "),
                           text(" n  Cancel ") | color(TextColor()) | bgcolor(BorderColor()) | reflect(cancel_box_),
                           text(" "),
                       }),
                       separator() | color(BorderColor()),
                       text(" y confirm · n / Esc cancel") | color(MutedColor()),
                   }) |
                   size(WIDTH, GREATER_THAN, 44) | size(WIDTH, LESS_THAN, 80) | borderStyled(ROUNDED, BorderColor()) |
                   bgcolor(PanelColor());
        });

        return CatchEvent(renderer, [this](Event event) {
            if (event == Event::Character('y') || event == Event::Character('Y'))
            {
                confirm();
                return true;
            }
            if (event == Event::Character('n') || event == Event::Character('N') || event == Event::Escape)
            {
                cancel();
                return true;
            }
            if (event.is_mouse() && event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed)
            {
                if (confirm_box_.Contain(event.mouse().x, event.mouse().y))
                    confirm();
                else if (cancel_box_.Contain(event.mouse().x, event.mouse().y))
                    cancel();
            }
            // Swallow everything else, Enter included.
            return true;
        });
    }

} // namespace gsystemctl::ui
