#pragma once

#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Live graphs on the right half of the detail pages.
namespace gsystemctl::ui
{

    // Last samples of one metric, oldest first.
    class MetricHistory
    {
    public:
        static constexpr size_t kCapacity = 300; // 10 minutes at one sample per 2s

        void push(double value)
        {
            values_.push_back(value);
            if (values_.size() > kCapacity)
                values_.pop_front();
        }
        void clear() { values_.clear(); }
        bool empty() const { return values_.empty(); }
        double last() const { return values_.empty() ? 0.0 : values_.back(); }
        double max() const { return values_.empty() ? 0.0 : *std::max_element(values_.begin(), values_.end()); }
        double min() const { return values_.empty() ? 0.0 : *std::min_element(values_.begin(), values_.end()); }
        const std::deque<double> &values() const { return values_; }

    private:
        std::deque<double> values_;
    };

    // Rate of a growing counter (bytes, CPU nanoseconds). Ignores resets.
    class CounterRate
    {
    public:
        // `seconds`: monotonic time of the sample. Returns per-second rate, or -1 until two samples exist.
        double update(double counter, double seconds)
        {
            double rate = -1.0;
            if (has_last_ && seconds > last_seconds_ && counter >= last_counter_)
                rate = (counter - last_counter_) / (seconds - last_seconds_);
            last_counter_ = counter;
            last_seconds_ = seconds;
            has_last_ = true;
            return rate;
        }
        void reset() { has_last_ = false; }

    private:
        bool has_last_ = false;
        double last_counter_ = 0.0;
        double last_seconds_ = 0.0;
    };

    inline std::string format_metric_bytes(double bytes)
    {
        const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        int unit = 0;
        while (bytes >= 1024.0 && unit < 4)
        {
            bytes /= 1024.0;
            ++unit;
        }
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s", bytes, units[unit]);
        return buffer;
    }
    inline std::string format_metric_rate(double bytes_per_second) { return format_metric_bytes(bytes_per_second) + "/s"; }
    inline std::string format_metric_percent(double percent)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f%%", percent);
        return buffer;
    }
    inline std::string format_metric_count(double count)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f", count);
        return buffer;
    }

    // Column chart of the newest samples, one column per sample, newest on
    // the right. Rates (`zero_based`) are scaled 0..largest visible value (at
    // least `floor`). Gauges like memory zoom in on their visible range, so
    // changes on a large base still show.
    class BarGraph : public ftxui::Node
    {
    public:
        BarGraph(std::vector<double> values, double floor, bool zero_based, ftxui::Color fg)
            : values_(std::move(values)), floor_(floor), zero_based_(zero_based), fg_(fg)
        {
        }

        void ComputeRequirement() override
        {
            requirement_.min_x = 4;
            requirement_.min_y = 1;
            requirement_.flex_grow_x = 1;
            requirement_.flex_grow_y = 1;
            requirement_.flex_shrink_x = 1;
            requirement_.flex_shrink_y = 1;
        }

        void Render(ftxui::Screen &screen) override
        {
            static const char *kEighths[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
            const int width = box_.x_max - box_.x_min + 1;
            const int height = box_.y_max - box_.y_min + 1;
            if (width <= 0 || height <= 0)
                return;
            const int count = std::min(width, static_cast<int>(values_.size()));
            const auto first = values_.end() - count;
            double high = 0.0;
            double low = count > 0 ? *first : 0.0;
            for (auto it = first; it != values_.end(); ++it)
            {
                high = std::max(high, *it);
                low = std::min(low, *it);
            }
            double bottom = 0.0;
            double top = std::max(high, floor_);
            if (!zero_based_)
            {
                const double range = std::max({high - low, high * 0.2, floor_});
                bottom = std::max(0.0, low - range * 0.5);
                top = high + range * 0.1;
            }

            for (int y = box_.y_min; y <= box_.y_max; ++y)
                for (int x = box_.x_min; x <= box_.x_max; ++x)
                    screen.PixelAt(x, y).background_color = theme::PanelColor();

            for (int i = 0; i < count; ++i)
            {
                const double value = std::max(0.0, *(first + i) - bottom);
                int eighths = static_cast<int>(std::lround(value / (top - bottom) * height * 8));
                if (value > 0 && eighths == 0)
                    eighths = 1; // keep non-zero samples visible
                const int x = box_.x_max - (count - 1 - i);
                for (int row = 0; row < height && eighths > 0; ++row, eighths -= 8)
                {
                    auto &pixel = screen.PixelAt(x, box_.y_max - row);
                    pixel.character = kEighths[std::min(eighths, 8)];
                    pixel.foreground_color = fg_;
                }
            }
        }

    private:
        std::vector<double> values_;
        double floor_;
        bool zero_based_;
        ftxui::Color fg_;
    };

    struct MetricView
    {
        std::string label;
        std::string current; // shown next to the label; empty while collecting
        const MetricHistory *history;
        std::string (*format)(double); // for the "max" label
        double floor;                  // smallest scale range
        ftxui::Color color;
        bool zero_based = true;        // false for gauges (memory, tasks)
    };

    // The graphs column. With room for at least two rows per metric, each gets
    // a header line and a graph, sharing the height equally; otherwise each is
    // one line with a one-row graph, so none is cut off.
    class GraphPanel
    {
    public:
        ftxui::Element render(const std::vector<MetricView> &metrics)
        {
            using namespace ftxui;
            // Height from the previous frame, like SummaryPane.
            const int height = box_.y_max - box_.y_min + 1;
            const bool compact = height > 0 && height < 2 * static_cast<int>(metrics.size());
            Elements blocks;
            for (const auto &m : metrics)
            {
                const bool collecting = m.history->empty();
                auto label = text(" " + m.label) | color(theme::HeaderColor()) | size(WIDTH, EQUAL, 11);
                auto value =
                    text(collecting ? "collecting…" : m.current) | bold | color(collecting ? theme::MutedColor() : m.color);
                auto range = text(collecting      ? ""
                                  : m.zero_based ? "max " + m.format(m.history->max()) + " "
                                                 : m.format(m.history->min()) + " – " + m.format(m.history->max()) + " ") |
                             color(theme::MutedColor());
                std::vector<double> values(m.history->values().begin(), m.history->values().end());
                auto graph = std::make_shared<BarGraph>(std::move(values), m.floor, m.zero_based, m.color);
                if (compact)
                {
                    blocks.push_back(hbox({label, value, text("  "), graph | flex, text(" ")}));
                }
                else
                {
                    blocks.push_back(vbox({
                                         hbox({label, value, filler(), range}),
                                         hbox({text(" "), graph, text(" ")}) | flex,
                                     }) |
                                     flex);
                }
            }
            return vbox(std::move(blocks)) | reflect(box_);
        }

    private:
        ftxui::Box box_;
    };

    namespace graph_colors
    {
        inline ftxui::Color Cpu() { return ftxui::Color::RGB(110, 171, 147); }
        inline ftxui::Color Memory() { return ftxui::Color::RGB(122, 162, 247); }
        inline ftxui::Color Tasks() { return ftxui::Color::RGB(187, 154, 247); }
        inline ftxui::Color Network() { return ftxui::Color::RGB(125, 207, 255); }
        inline ftxui::Color Disk() { return ftxui::Color::RGB(224, 175, 104); }
    } // namespace graph_colors

} // namespace gsystemctl::ui
