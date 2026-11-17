#include "g-systemctl/ui/containers_view.hpp"
#include "g-systemctl/ui/styles.hpp"
#include "g-systemctl/ui/theme.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/terminal.hpp>
#include <functional>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace ftxui;

namespace gsystemctl::ui
{

    namespace
    {
        // Takes the rest of the row and is clipped when there is none: its
        // width never pushes the columns before it, so rows stay aligned.
        class Elastic : public Node
        {
        public:
            explicit Elastic(Element child) : Node({std::move(child)}) {}
            void ComputeRequirement() override
            {
                children_[0]->ComputeRequirement();
                requirement_ = children_[0]->requirement();
                requirement_.min_x = 0;
                requirement_.flex_grow_x = 1;
                requirement_.flex_shrink_x = 1;
            }
            void SetBox(Box box) override
            {
                Node::SetBox(box);
                children_[0]->SetBox(box);
            }
        };
        Element elastic(Element child) { return std::make_shared<Elastic>(std::move(child)); }
    } // namespace

    using namespace theme;

    namespace
    {
        constexpr auto kPollInterval = std::chrono::seconds(2);
        constexpr int kLogTail = 200;

        std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return value;
        }

        struct StateStyle
        {
            std::string label;
            Color fg;
        };

        StateStyle state_style(const std::string &state)
        {
            if (state == "running")
                return {"● running", Colors::running_fg()};
            if (state == "paused")
                return {"◐ paused", WarningColor()};
            if (state == "restarting")
                return {"↻ restarting", WarningColor()};
            if (state == "dead")
                return {"✕ dead", Colors::error_fg()};
            return {"○ " + state, Colors::stopped_fg()};
        }

        bool looks_secret(const std::string &env_entry)
        {
            auto key = lower(env_entry.substr(0, env_entry.find('=')));
            for (const char *marker : {"pass", "secret", "token", "key", "credential", "auth"})
            {
                if (key.find(marker) != std::string::npos)
                    return true;
            }
            return false;
        }

        // Unix timestamp with nanoseconds, the format `docker logs --since` takes.
        std::string unix_time_now()
        {
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld.%09lld", static_cast<long long>(ns / 1000000000),
                          static_cast<long long>(ns % 1000000000));
            return buffer;
        }

        std::string limit_text(const std::string &value, const std::string &unlimited = "unlimited")
        {
            return value.empty() ? unlimited : value;
        }

        const std::string kProjectKey = "project:";

        RowActivity::Labels compose_row_labels(ComposeAction action)
        {
            switch (action)
            {
            case ComposeAction::Start:
                return {"starting", "started", "start failed"};
            case ComposeAction::Stop:
                return {"stopping", "stopped", "stop failed"};
            case ComposeAction::Restart:
                return {"restarting", "restarted", "restart failed"};
            case ComposeAction::Up:
                return {"bringing up", "up", "up failed"};
            case ComposeAction::UpBuild:
                return {"building", "built and up", "build failed"};
            case ComposeAction::Pull:
                return {"pulling", "pulled", "pull failed"};
            case ComposeAction::Down:
                return {"taking down", "down", "down failed"};
            }
            return {};
        }
        // ListRow::index of a divider: which group it labels.
        constexpr int kComposeSection = 0;
        constexpr int kStandaloneSection = 1;
        // Drift for every project is checked every this many polls (~30 s); the open project on every poll.
        constexpr int kDriftEvery = 15;

        std::string trim(const std::string &value)
        {
            auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos)
                return "";
            auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        std::string join(const std::vector<std::string> &items, const std::string &sep)
        {
            std::string out;
            for (const auto &item : items)
                out += (out.empty() ? "" : sep) + item;
            return out;
        }

        StateStyle project_style(const ComposeProject &p)
        {
            const auto count = std::to_string(p.running) + "/" + std::to_string(p.total);
            if (p.down())
                return {"○ down", Colors::stopped_fg()};
            if (p.running == p.total)
                return {"● " + count + " up", Colors::running_fg()};
            if (p.active > 0)
                return {"◐ " + count + " up", WarningColor()};
            return {"○ stopped", Colors::stopped_fg()};
        }

        // "web", "worker #2", "api (run)".
        std::string service_label(const Container &c, bool multi_replica)
        {
            if (c.compose_service.empty())
                return c.name;
            std::string label = c.compose_service;
            if (multi_replica || c.compose_number > 1)
                label += " #" + std::to_string(c.compose_number);
            if (c.compose_oneoff)
                label += " (run)";
            return label;
        }

        // "0.0.0.0:8080->80/tcp, [::]:8080->80/tcp, 9000/tcp" -> "8080->80/tcp, 9000/tcp".
        std::string short_ports(const std::string &ports)
        {
            std::vector<std::string> kept;
            size_t start = 0;
            while (start <= ports.size())
            {
                auto end = ports.find(", ", start);
                auto entry = trim(ports.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (entry.rfind("0.0.0.0:", 0) == 0)
                    entry.erase(0, 8);
                if (!entry.empty() && entry.rfind("[::]:", 0) != 0 &&
                    std::find(kept.begin(), kept.end(), entry) == kept.end())
                    kept.push_back(entry);
                if (end == std::string::npos)
                    break;
                start = end + 2;
            }
            return join(kept, ", ");
        }

        struct Totals
        {
            double cpu = 0, memory = 0, pids = 0;
            int sampled = 0;
        };
        Totals project_totals(const std::vector<Container> &containers, const std::string &project)
        {
            Totals t;
            for (const auto &c : containers)
            {
                if (c.compose_project != project || !c.is_running() || c.stats.cpu.empty())
                    continue;
                ++t.sampled;
                t.cpu += std::max(0.0, parse_docker_percent(c.stats.cpu));
                t.memory += std::max(0.0, parse_docker_size_pair(c.stats.mem_usage).first);
                t.pids += std::max(0.0, parse_docker_size(c.stats.pids));
            }
            return t;
        }

        // Columns of the containers list with their headers; STATUS (uptime
        // text) does not sort. Alt+s cycles through the sortable ones.
        struct ListColumn
        {
            std::string header;
            bool sortable;
            SortColumn column;
        };
        const std::vector<ListColumn> kListColumns = {
            {"CONTAINER", true, SortColumn::Name}, {"IMAGE", true, SortColumn::Image},
            {"STATE", true, SortColumn::State},    {"STATUS", false, SortColumn::Name},
            {"CPU", true, SortColumn::Cpu},        {"MEM", true, SortColumn::Memory},
            {"PIDS", true, SortColumn::Pids},
        };

        // Running first, then paused/restarting/created, then stopped.
        int state_rank(const std::string &state)
        {
            if (state == "running")
                return 0;
            if (state == "paused" || state == "restarting" || state == "created")
                return 1;
            return 2;
        }

        SortValue container_sort_value(const Container &c, SortColumn column)
        {
            SortValue v;
            v.name = c.compose_service.empty() ? c.name : c.compose_service;
            const bool sampled = c.is_running() && !c.stats.cpu.empty();
            switch (column)
            {
            case SortColumn::Image:
                v.text = c.image;
                break;
            case SortColumn::State:
                v.number = state_rank(c.state);
                break;
            case SortColumn::Cpu:
                v.number = parse_docker_percent(c.stats.cpu);
                v.missing = !sampled;
                break;
            case SortColumn::Memory:
                v.number = parse_docker_size_pair(c.stats.mem_usage).first;
                v.missing = !sampled;
                break;
            case SortColumn::Pids:
                v.number = parse_docker_size(c.stats.pids);
                v.missing = !sampled;
                break;
            default:
                v.text = v.name;
                break;
            }
            return v;
        }

        SortValue project_sort_value(const ComposeProject &p, const std::vector<Container> &containers,
                                     SortColumn column)
        {
            SortValue v;
            v.name = p.ref.name;
            const auto totals = project_totals(containers, p.ref.name);
            switch (column)
            {
            case SortColumn::State:
                v.number = p.down() ? 3 : p.running == p.total ? 0 : p.running > 0 ? 1 : 2;
                break;
            case SortColumn::Cpu:
                v.number = totals.cpu;
                v.missing = !totals.sampled;
                break;
            case SortColumn::Memory:
                v.number = totals.memory;
                v.missing = !totals.sampled;
                break;
            case SortColumn::Pids:
                v.number = totals.pids;
                v.missing = !totals.sampled;
                break;
            default: // Name, and Image (a project has none)
                v.text = p.ref.name;
                break;
            }
            return v;
        }
    } // namespace

    void ContainersView::sort_by(SortColumn column)
    {
        if (sort_.column == column)
            sort_.descending = !sort_.descending;
        else
            sort_ = {column, default_descending(column)};
        apply_filter();
    }

    ContainersView::ContainersView(ScreenInteractive &screen, std::shared_ptr<ContainerManager> manager,
                                   const std::string &compose_store_path)
        : screen_(screen), manager_(std::move(manager)), compose_store_(compose_store_path), logs_(screen)
    {
        poller_ = std::thread([this] { poll_loop(); });
    }

    ContainersView::~ContainersView()
    {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_stop_ = true;
        }
        poll_cv_.notify_all();
        if (poller_.joinable())
            poller_.join();
        if (action_thread_.joinable())
            action_thread_.join();
        for (auto &job : shell_jobs_)
            job.thread.join();
    }

    // ---- polling ------------------------------------------------------------

    void ContainersView::set_active(bool active)
    {
        active_ = active;
        if (!active)
            shell_open_pending_.clear();
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_active_ = active;
            poll_requested_ = active;
        }
        poll_cv_.notify_all();
    }

    void ContainersView::refresh_now()
    {
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            poll_requested_ = true;
        }
        poll_cv_.notify_all();
    }

    void ContainersView::poll_loop()
    {
        bool runtime_ok = false;
        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(poll_mutex_);
                poll_cv_.wait(lock, [this] { return poll_stop_ || poll_active_; });
                if (poll_stop_)
                    return;
                poll_requested_ = false;
            }

            poll_once(runtime_ok);

            {
                std::unique_lock<std::mutex> lock(poll_mutex_);
                poll_cv_.wait_for(lock, kPollInterval, [this] { return poll_stop_ || poll_requested_; });
                if (poll_stop_)
                    return;
            }
        }
    }

    void ContainersView::poll_once(bool &runtime_ok)
    {
        if (!runtime_ok)
        {
            auto [ok, message] = manager_->check_available();
            runtime_ok = ok;
            screen_.Post([this, ok = ok, message = message] {
                const bool first = !checked_;
                checked_ = true;
                available_ = ok;
                availability_message_ = message;
                if (first && !ok && on_unavailable_)
                    on_unavailable_();
            });
            screen_.PostEvent(Event::Custom);
            if (!ok)
                return;
        }

        if (!poller_compose_checked_)
        {
            poller_compose_checked_ = true;
            auto [ok, message] = manager_->compose_available();
            poller_compose_ok_ = ok;
            poller_drift_ok_ = ok && manager_->compose_drift_supported();
            screen_.Post([this, ok = ok, message = message, drift_ok = poller_drift_ok_] {
                compose_ok_ = ok;
                compose_message_ = message;
                drift_supported_ = drift_ok;
            });
        }

        // Which drift checks this poll does. Read before listing: a request made
        // after the list was taken (e.g. by a finished `up -d`) must wait for the
        // next poll, or it would be answered with the old containers' labels.
        bool check_all = false;
        std::string open_project;
        {
            std::lock_guard<std::mutex> lock(poll_mutex_);
            check_all = poll_drift_all_ || ++poll_count_ % kDriftEvery == 0;
            poll_drift_all_ = false;
            open_project = poll_project_;
        }

        std::vector<Container> listed;
        try
        {
            auto listed_at = unix_time_now();
            auto containers = manager_->list_containers();
            listed = containers;
            screen_.Post([this, containers = std::move(containers), listed_at]() mutable {
                set_containers(std::move(containers));
                reattach_logs_if_running(listed_at);
            });
            screen_.PostEvent(Event::Custom);
        }
        catch (const std::exception &e)
        {
            runtime_ok = false;
            if (check_all)
            {
                std::lock_guard<std::mutex> lock(poll_mutex_);
                poll_drift_all_ = true;
            }
            std::string message = e.what();
            screen_.Post([this, message] {
                available_ = false;
                availability_message_ = message;
            });
            screen_.PostEvent(Event::Custom);
            return;
        }

        try
        {
            auto stats = manager_->container_stats();
            screen_.Post([this, stats = std::move(stats)]() mutable {
                set_stats(std::move(stats));
            });
            screen_.PostEvent(Event::Custom);
        }
        catch (const std::exception &)
        {
            // Stats are best-effort; the list is still useful without them.
        }

        // Compare each compose service's config hash with its files as they are now.
        if (!poller_drift_ok_ || (!check_all && open_project.empty()))
            return;
        std::map<std::string, std::vector<std::string>> drift;
        std::map<std::string, std::string> errors;
        for (const auto &p : group_compose_projects(listed, {}))
        {
            if (!check_all && p.ref.name != open_project)
                continue;
            try
            {
                drift[p.ref.name] = compose_drifted_services(listed, p.ref.name, manager_->compose_config_hashes(p.ref));
            }
            catch (const std::exception &e)
            {
                errors[p.ref.name] = e.what();
            }
        }
        screen_.Post([this, drift = std::move(drift), errors = std::move(errors), check_all]() mutable {
            set_drift(std::move(drift), std::move(errors), check_all);
        });
        screen_.PostEvent(Event::Custom);
    }

    void ContainersView::set_poll_project(const std::string &name, bool check_all)
    {
        std::lock_guard<std::mutex> lock(poll_mutex_);
        poll_project_ = name;
        poll_drift_all_ = poll_drift_all_ || check_all;
    }

    void ContainersView::set_drift(std::map<std::string, std::vector<std::string>> drift,
                                   std::map<std::string, std::string> errors, bool all)
    {
        if (all)
        {
            drift_ = std::move(drift);
            drift_error_ = std::move(errors);
            return;
        }
        for (auto &[name, services] : drift)
        {
            drift_error_.erase(name);
            drift_[name] = std::move(services);
        }
        for (auto &[name, error] : errors)
        {
            drift_.erase(name);
            drift_error_[name] = std::move(error);
        }
    }

    void ContainersView::remember_projects()
    {
        for (const auto &p : group_compose_projects(containers_, {}))
        {
            if (p.total > 0)
                compose_store_.remember(p.ref);
        }
    }

    void ContainersView::set_containers(std::vector<Container> containers)
    {
        containers_ = std::move(containers);
        for (auto &c : containers_)
        {
            auto it = stats_.find(c.id);
            c.stats = (it != stats_.end() && c.is_running()) ? it->second : ContainerStats{};
        }
        remember_projects();
        apply_filter();
    }

    void ContainersView::set_stats(std::map<std::string, ContainerStats> stats)
    {
        stats_ = std::move(stats);
        record_stats();
        set_containers(std::move(containers_));
    }

    void ContainersView::record_stats()
    {
        static const auto start = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (auto it = metrics_.begin(); it != metrics_.end();)
        {
            it = stats_.count(it->first) ? std::next(it) : metrics_.erase(it);
        }
        for (const auto &[id, s] : stats_)
        {
            auto &m = metrics_[id];
            if (double cpu = parse_docker_percent(s.cpu); cpu >= 0)
                m.cpu.push(cpu);
            if (double used = parse_docker_size_pair(s.mem_usage).first; used >= 0)
                m.memory.push(used);
            if (double pids = parse_docker_size(s.pids); pids >= 0)
                m.pids.push(pids);
            // Both I/O columns are totals since start; graph their rate.
            auto rates = [seconds](const std::string &pair, CounterRate &a, CounterRate &b, double &a_rate,
                                   double &b_rate, MetricHistory &history) {
                auto [first, second] = parse_docker_size_pair(pair);
                if (first < 0 || second < 0)
                    return;
                a_rate = a.update(first, seconds);
                b_rate = b.update(second, seconds);
                if (a_rate >= 0 && b_rate >= 0)
                    history.push(a_rate + b_rate);
            };
            rates(s.net_io, m.net_rx, m.net_tx, m.net_rx_per_s, m.net_tx_per_s, m.net);
            rates(s.block_io, m.block_read, m.block_write, m.read_per_s, m.write_per_s, m.block);
        }

        // Project graphs: the sum over its running containers.
        struct Sum
        {
            double cpu = 0, memory = 0, pids = 0, rx = 0, tx = 0, read = 0, write = 0;
            bool rates = true;
        };
        std::map<std::string, Sum> sums;
        for (const auto &c : containers_)
        {
            auto it = metrics_.find(c.id);
            if (c.compose_project.empty() || it == metrics_.end())
                continue;
            const auto &m = it->second;
            auto &sum = sums[c.compose_project];
            sum.cpu += m.cpu.last();
            sum.memory += m.memory.last();
            sum.pids += m.pids.last();
            if (m.net_rx_per_s < 0 || m.net_tx_per_s < 0 || m.read_per_s < 0 || m.write_per_s < 0)
            {
                sum.rates = false;
                continue;
            }
            sum.rx += m.net_rx_per_s;
            sum.tx += m.net_tx_per_s;
            sum.read += m.read_per_s;
            sum.write += m.write_per_s;
        }
        for (auto it = project_metrics_.begin(); it != project_metrics_.end();)
        {
            it = sums.count(it->first) ? std::next(it) : project_metrics_.erase(it);
        }
        for (const auto &[name, sum] : sums)
        {
            auto &pm = project_metrics_[name];
            pm.cpu.push(sum.cpu);
            pm.memory.push(sum.memory);
            pm.pids.push(sum.pids);
            if (sum.rates)
            {
                pm.net_rx_per_s = sum.rx;
                pm.net_tx_per_s = sum.tx;
                pm.read_per_s = sum.read;
                pm.write_per_s = sum.write;
                pm.net.push(sum.rx + sum.tx);
                pm.block.push(sum.read + sum.write);
            }
        }
    }

    Element ContainersView::render_graphs()
    {
        const auto *c = action_target();
        if (!c || !c->is_running())
            return nullptr;
        auto it = metrics_.find(c->id);
        if (it == metrics_.end())
            return nullptr;
        const auto &m = it->second;
        auto rate = [](double v) { return v < 0 ? std::string("-") : format_metric_rate(v); };
        std::vector<MetricView> views = {
            {"CPU", format_metric_percent(m.cpu.last()), &m.cpu, format_metric_percent, 1.0, graph_colors::Cpu()},
            {"Memory", format_metric_bytes(m.memory.last()), &m.memory, format_metric_bytes, 1024.0 * 1024.0,
             graph_colors::Memory()},
            {"PIDs", format_metric_count(m.pids.last()), &m.pids, format_metric_count, 1.0, graph_colors::Tasks(), false},
            {"Net I/O", "↓ " + rate(m.net_rx_per_s) + "  ↑ " + rate(m.net_tx_per_s), &m.net, format_metric_rate,
             1024.0, graph_colors::Network()},
            {"Disk I/O", "R " + rate(m.read_per_s) + "  W " + rate(m.write_per_s), &m.block, format_metric_rate,
             1024.0, graph_colors::Disk()},
        };
        return graph_panel_.render(views);
    }

    // ---- selection / filter ---------------------------------------------------

    void ContainersView::set_filter(const std::string &filter)
    {
        filter_text_ = filter;
        apply_filter(true);
    }

    void ContainersView::apply_filter(bool first_if_gone)
    {
        filtered_.clear();
        rows_.clear();
        multi_replica_.clear();
        projects_ = group_compose_projects(containers_, compose_store_.projects());

        const auto needle = lower(filter_text_);
        auto contains = [&needle](const std::string &value) { return lower(value).find(needle) != std::string::npos; };
        auto matches = [&](const Container &c) {
            return needle.empty() || contains(c.name) || contains(c.image) || contains(c.compose_project) ||
                   contains(c.compose_service);
        };
        auto add = [this](const Container &c) {
            filtered_.push_back(c);
            return static_cast<int>(filtered_.size()) - 1;
        };
        std::map<std::string, const Container *> by_id;
        std::map<std::string, int> replicas;
        for (const auto &c : containers_)
        {
            by_id[c.id] = &c;
            if (!c.compose_project.empty() && !c.compose_oneoff)
                ++replicas[c.compose_project + "/" + c.compose_service];
        }
        for (const auto &[key, count] : replicas)
        {
            if (count > 1)
                multi_replica_.insert(key);
        }

        // Projects, their containers and standalone containers each sort
        // within their own group.
        const auto order = sort_;
        auto before = [&order](const SortValue &a, const SortValue &b) {
            return sort_before(a, b, order.descending);
        };
        std::vector<size_t> project_order(projects_.size());
        for (size_t i = 0; i < project_order.size(); ++i)
            project_order[i] = i;
        std::stable_sort(project_order.begin(), project_order.end(), [&](size_t a, size_t b) {
            return before(project_sort_value(projects_[a], containers_, order.column),
                          project_sort_value(projects_[b], containers_, order.column));
        });
        auto sorted_ids = [&](std::vector<std::string> ids) {
            std::stable_sort(ids.begin(), ids.end(), [&](const std::string &a, const std::string &b) {
                auto x = by_id.find(a), y = by_id.find(b);
                if (x == by_id.end() || y == by_id.end())
                    return false;
                return before(container_sort_value(*x->second, order.column),
                              container_sort_value(*y->second, order.column));
            });
            return ids;
        };

        bool any_project = false;
        for (size_t pi : project_order)
        {
            const auto &p = projects_[pi];
            // A matching project shows all its containers.
            const bool project_match = needle.empty() || contains(p.ref.name);
            std::vector<int> children;
            for (const auto &id : sorted_ids(p.container_ids))
            {
                auto it = by_id.find(id);
                if (it != by_id.end() && (project_match || matches(*it->second)))
                    children.push_back(add(*it->second));
            }
            if (!project_match && children.empty())
                continue;
            any_project = true;
            rows_.push_back({ListRow::Kind::Project, static_cast<int>(pi)});
            // While filtering, matches are always shown.
            if (needle.empty() && collapsed_.count(p.ref.name))
                continue;
            for (size_t k = 0; k < children.size(); ++k)
                rows_.push_back({ListRow::Kind::Container, children[k], k + 1 == children.size()});
        }

        std::vector<std::string> standalone_ids;
        for (const auto &c : containers_)
            if (c.compose_project.empty())
                standalone_ids.push_back(c.id);
        bool divider = false;
        for (const auto &id : sorted_ids(standalone_ids))
        {
            const auto &c = *by_id[id];
            if (!matches(c))
                continue;
            if (any_project && !divider)
            {
                rows_.push_back({ListRow::Kind::Divider, kStandaloneSection});
                divider = true;
            }
            rows_.push_back({ListRow::Kind::Container, add(c)});
        }
        // Label both groups only when both are listed.
        if (divider)
            rows_.insert(rows_.begin(), {ListRow::Kind::Divider, kComposeSection});

        // Keep the same row selected across refreshes and re-sorts.
        auto key_of = [this](const ListRow &row) -> std::string {
            if (row.kind == ListRow::Kind::Project)
                return kProjectKey + projects_[row.index].ref.name;
            if (row.kind == ListRow::Kind::Container)
                return filtered_[row.index].id;
            return "";
        };
        auto find_key = [&](const std::string &key) {
            for (size_t i = 0; i < rows_.size(); ++i)
            {
                if (!key.empty() && key_of(rows_[i]) == key)
                    return static_cast<int>(i);
            }
            return -1;
        };
        int index = find_key(selected_key_);
        if (index < 0)
        {
            // A container hidden by folding its project: select the project.
            auto it = by_id.find(selected_key_);
            if (it != by_id.end() && !it->second->compose_project.empty())
                index = find_key(kProjectKey + it->second->compose_project);
        }
        if (index < 0)
            index = first_if_gone ? 0 : std::clamp(selected_index_, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
        select(index, rows_.empty() || index < static_cast<int>(rows_.size()) - 1 ? 1 : -1);
        if (rows_.empty())
            selected_key_.clear();
    }

    void ContainersView::select(int index, int step)
    {
        if (rows_.empty())
            return;
        const int last = static_cast<int>(rows_.size()) - 1;
        index = std::clamp(index, 0, last);
        // Dividers cannot be selected; move past them, or back if at an end.
        if (rows_[index].kind == ListRow::Kind::Divider)
        {
            int next = index + step;
            if (next < 0 || next > last)
                next = index - step;
            index = std::clamp(next, 0, last);
        }
        selected_index_ = index;
        const auto &row = rows_[index];
        if (row.kind == ListRow::Kind::Project)
            selected_key_ = kProjectKey + projects_[row.index].ref.name;
        else if (row.kind == ListRow::Kind::Container)
            selected_key_ = filtered_[row.index].id;
    }

    const Container *ContainersView::selected() const
    {
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(rows_.size()))
            return nullptr;
        const auto &row = rows_[selected_index_];
        return row.kind == ListRow::Kind::Container ? &filtered_[row.index] : nullptr;
    }

    const ComposeProject *ContainersView::selected_project() const
    {
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(rows_.size()))
            return nullptr;
        const auto &row = rows_[selected_index_];
        return row.kind == ListRow::Kind::Project ? &projects_[row.index] : nullptr;
    }

    const ComposeProject *ContainersView::find_project(const std::string &name) const
    {
        auto it = std::find_if(projects_.begin(), projects_.end(),
                               [&name](const ComposeProject &p) { return p.ref.name == name; });
        return it == projects_.end() ? nullptr : &*it;
    }

    const Container *ContainersView::find_container(const std::string &id) const
    {
        auto it = std::find_if(containers_.begin(), containers_.end(), [&id](const Container &c) { return c.id == id; });
        return it == containers_.end() ? nullptr : &*it;
    }

    void ContainersView::clear_messages()
    {
        status_message_.clear();
        error_message_.clear();
    }

    std::string ContainersView::summary() const
    {
        auto text = std::to_string(filtered_.size()) + "/" + std::to_string(containers_.size()) +
                    (containers_.size() == 1 ? " container" : " containers");
        if (!projects_.empty())
            text += " · " + std::to_string(projects_.size()) + (projects_.size() == 1 ? " project" : " projects");
        return text;
    }

    std::string ContainersView::hints() const
    {
        if (logs_full_)
            return "";
        if (detail_open_ && !detail_project_.empty())
            return "  ·  Alt+u up  ·  Alt+d down  ·  Enter logs  ·  Esc back  ·  ? help  ";
        if (detail_open_)
        {
            const auto *c = action_target();
            if (!c)
                return "";
            auto it = shells_.find(c->id);
            const bool shell = c->is_running() && it != shells_.end() && it->second.ok;
            if (c->compose_project.empty() && !shell)
                return "";
            return std::string("  ·  ↑↓ scroll info") + (shell ? "  ·  Alt+t shell" : "") +
                   (c->compose_project.empty() ? "" : "  ·  Alt+v project") +
                   "  ·  Enter expand logs  ·  Esc back  ·  ? help  ";
        }
        if (selected_project())
            return "  ·  ←→ fold  ·  Enter open  ·  Alt+o start  ·  Alt+x stop  ·  Alt+d down  ·  Alt+a add  ·  ? help  ";
        return "";
    }

    // ---- actions --------------------------------------------------------------

    void ContainersView::run_action(const std::string &busy_message, std::function<ContainerManager::Result()> action,
                                    const std::string &success_message, const std::string &row_key,
                                    const RowActivity::Labels &labels)
    {
        if (busy_)
            return;
        busy_ = true;
        busy_message_ = busy_message;
        clear_messages();
        if (!row_key.empty())
            row_activity_.start(row_key, labels.busy);

        if (action_thread_.joinable())
            action_thread_.join();
        action_thread_ = std::thread([this, action = std::move(action), success_message, busy_message, row_key,
                                      labels] {
            auto [ok, message] = action();
            screen_.Post([this, ok = ok, message = message, success_message, busy_message, row_key, labels] {
                busy_ = false;
                busy_message_.clear();
                if (!row_key.empty())
                    row_activity_.finish(row_key, ok, ok ? labels.done : labels.failed);
                if (ok)
                {
                    status_message_ = success_message;
                }
                else
                {
                    error_message_ = busy_message.substr(0, busy_message.size() - 3) + " failed: " + message;
                }
                if (detail_open_)
                    reload_details();
                // The action may have changed which config the containers run.
                set_poll_project(detail_project_, true);
                refresh_now();
            });
            screen_.PostEvent(Event::Custom);
        });
    }

    void ContainersView::report_removed()
    {
        // On the page of a container that no longer exists, say so instead of doing nothing.
        if (detail_open_ && detail_project_.empty())
        {
            clear_messages();
            error_message_ = logs_name_ + " was removed";
        }
    }

    const Container *ContainersView::action_target() const
    {
        auto find = [this](auto pred) -> const Container * {
            auto it = std::find_if(containers_.begin(), containers_.end(), pred);
            return it == containers_.end() ? nullptr : &*it;
        };
        if (!detail_open_)
            return selected();
        if (!detail_project_.empty())
            return nullptr;
        if (const auto *c = find([this](const Container &c) { return c.id == logs_id_; }))
            return c;
        // Recreated with a new ID and not reattached yet.
        return find([this](const Container &c) { return c.name == logs_name_; });
    }

    void ContainersView::start_target()
    {
        if (const auto *p = project_target())
        {
            clear_messages();
            if (p->down())
                run_compose(*p, ComposeAction::Up);
            else if (p->active == p->total)
                status_message_ = p->ref.name + " is already running (Alt+u applies config changes)";
            else
                run_compose(*p, ComposeAction::Start);
            return;
        }
        const auto *c = action_target();
        if (!c)
        {
            report_removed();
            return;
        }
        clear_messages();
        if (c->is_active())
        {
            status_message_ = c->name + " is already running";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        run_action("Starting " + name + "...", [this, id] { return manager_->start(id); }, "Started " + name, id,
                   {"starting", "started", "start failed"});
    }

    void ContainersView::stop_target()
    {
        if (const auto *p = project_target())
        {
            clear_messages();
            if (p->active == 0)
                status_message_ = p->ref.name + " is not running (Alt+o to start)";
            else
                confirm_compose(*p, ComposeAction::Stop, "Stop all containers of compose project " + p->ref.name + "?",
                                "Stop");
            return;
        }
        const auto *c = action_target();
        if (!c)
        {
            report_removed();
            return;
        }
        clear_messages();
        if (!c->is_active())
        {
            status_message_ = c->name + " is not running (Alt+o to start)";
            return;
        }
        if (!confirm_)
        {
            error_message_ = "Stop needs confirmation, which is unavailable";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        confirm_("Stop container " + name + "?", "Stop", [this, id, name] {
            run_action("Stopping " + name + "...", [this, id] { return manager_->stop(id); }, "Stopped " + name, id,
                       {"stopping", "stopped", "stop failed"});
        });
    }

    void ContainersView::restart_target()
    {
        if (const auto *p = project_target())
        {
            clear_messages();
            if (p->active == 0)
                status_message_ = p->ref.name + " is not running (Alt+o to start)";
            else
                confirm_compose(*p, ComposeAction::Restart,
                                "Restart all containers of compose project " + p->ref.name + "?", "Restart");
            return;
        }
        const auto *c = action_target();
        if (!c)
        {
            report_removed();
            return;
        }
        clear_messages();
        if (!c->is_active())
        {
            status_message_ = c->name + " is not running (Alt+o to start)";
            return;
        }
        if (!confirm_)
        {
            error_message_ = "Restart needs confirmation, which is unavailable";
            return;
        }
        auto id = c->id;
        auto name = c->name;
        confirm_("Restart container " + name + "?", "Restart", [this, id, name] {
            run_action("Restarting " + name + "...", [this, id] { return manager_->restart(id); },
                       "Restarted " + name, id, {"restarting", "restarted", "restart failed"});
        });
    }

    // ---- compose actions ---------------------------------------------------------

    const ComposeProject *ContainersView::project_target() const
    {
        if (!detail_open_)
            return selected_project();
        return detail_project_.empty() ? nullptr : find_project(detail_project_);
    }

    const ComposeProject *ContainersView::require_project()
    {
        clear_messages();
        if (const auto *p = project_target())
            return p;
        const auto *c = action_target();
        if (c && !c->compose_project.empty())
            status_message_ = "Compose actions work on the project: select its row, or Alt+v";
        else
            status_message_ = "Not a compose project";
        return nullptr;
    }

    void ContainersView::run_compose(const ComposeProject &project, ComposeAction action)
    {
        if (!compose_ok_)
        {
            error_message_ = compose_message_.empty() ? "compose is not available" : compose_message_;
            return;
        }
        auto ref = project.ref;
        run_action(compose_action_verb(action) + " " + ref.name + "...",
                   [this, ref, action] { return manager_->compose(ref, action); },
                   compose_action_past(action) + " " + ref.name, kProjectKey + ref.name, compose_row_labels(action));
    }

    void ContainersView::confirm_compose(const ComposeProject &project, ComposeAction action,
                                         const std::string &question, const std::string &label)
    {
        if (!confirm_)
        {
            error_message_ = label + " needs confirmation, which is unavailable";
            return;
        }
        auto name = project.ref.name;
        confirm_(question, label, [this, name, action] {
            // The list may have refreshed while the prompt was open.
            if (const auto *p = find_project(name))
                run_compose(*p, action);
        });
    }

    void ContainersView::compose_up_target()
    {
        const auto *p = require_project();
        if (!p)
            return;
        if (p->down())
        {
            run_compose(*p, ComposeAction::Up);
            return;
        }
        confirm_compose(*p, ComposeAction::Up,
                        "Run `compose up -d` for " + p->ref.name +
                            "? Services whose config or image changed are recreated.",
                        "Up");
    }

    void ContainersView::compose_build_target()
    {
        const auto *p = require_project();
        if (!p)
            return;
        confirm_compose(*p, ComposeAction::UpBuild,
                        "Rebuild images and run `compose up -d` for " + p->ref.name + "?", "Build");
    }

    void ContainersView::compose_pull_target()
    {
        const auto *p = require_project();
        if (!p)
            return;
        run_compose(*p, ComposeAction::Pull);
    }

    void ContainersView::compose_down_target()
    {
        const auto *p = require_project();
        if (!p)
            return;
        if (!confirm_)
        {
            error_message_ = "Down needs confirmation, which is unavailable";
            return;
        }
        auto name = p->ref.name;
        if (p->down())
        {
            // Nothing to take down: offer to drop it from the saved list.
            confirm_("Forget compose project " + name + "? It leaves this list; its files are not touched.", "Forget",
                     [this, name] {
                         compose_store_.forget(name);
                         if (detail_open_ && detail_project_ == name)
                             close_detail();
                         apply_filter();
                         status_message_ = "Forgot " + name;
                     });
            return;
        }
        confirm_compose(*p, ComposeAction::Down,
                        "Take down " + name + "? Its containers and networks are removed; volumes are kept.", "Down");
    }

    // ---- shell ---------------------------------------------------------------------

    void ContainersView::shell_target()
    {
        clear_messages();
        if (project_target())
        {
            status_message_ = "A shell opens in a container: select one of the project's containers";
            return;
        }
        const auto *c = action_target();
        if (!c)
        {
            report_removed();
            return;
        }
        if (!c->is_running())
        {
            status_message_ = c->name + " is not running (Alt+o to start)";
            return;
        }
        auto it = shells_.find(c->id);
        if (it != shells_.end() && it->second.ok)
        {
            enter_shell(c->id, c->name, it->second.shell);
            return;
        }
        status_message_ = "Looking for a shell in " + c->name + "...";
        detect_shell(c->id, true);
    }

    void ContainersView::detect_shell(const std::string &id, bool open_after)
    {
        auto &info = shells_[id];
        if (open_after)
            shell_open_pending_ = id;
        if (info.ok || info.checking)
            return;
        info.checking = true;
        info.reason.clear();
        shell_jobs_.erase(std::remove_if(shell_jobs_.begin(), shell_jobs_.end(),
                                         [](ShellJob &job) {
                                             if (!*job.done)
                                                 return false;
                                             job.thread.join(); // already returned
                                             return true;
                                         }),
                          shell_jobs_.end());
        auto done = std::make_shared<std::atomic<bool>>(false);
        shell_jobs_.push_back({std::thread([this, id, done] {
            auto [ok, value] = manager_->detect_shell(id);
            screen_.Post([this, id, ok = ok, value = value] {
                auto &info = shells_[id];
                info.checking = false;
                info.ok = ok;
                info.shell = ok ? value : "";
                info.reason = ok ? "" : value;
                if (shell_open_pending_ != id)
                    return;
                shell_open_pending_.clear();
                // Only if that container is still the one the user is on.
                const auto *target = active_ ? action_target() : nullptr;
                if (!target || target->id != id)
                {
                    status_message_.clear();
                    return;
                }
                const auto *c = find_container(id);
                const std::string name = c ? c->name : id;
                if (ok)
                {
                    status_message_.clear();
                    enter_shell(id, name, value);
                }
                else
                {
                    status_message_.clear();
                    error_message_ = "No shell in " + name + ": " + value;
                }
            });
            screen_.PostEvent(Event::Custom);
            *done = true;
        }), done});
    }

    void ContainersView::enter_shell(const std::string &id, const std::string &name, const std::string &shell)
    {
        int exit_code = 0;
        bool quick = false;
        screen_.WithRestoredIO([&] {
            std::cout << "\n── " << shell << " in " << name << " · exit or Ctrl+D returns to g-systemctl ──\n"
                      << std::flush;
            auto started = std::chrono::steady_clock::now();
            exit_code = manager_->open_shell(id, shell);
            quick = std::chrono::steady_clock::now() - started < std::chrono::seconds(1);
            if (exit_code != 0 && quick)
            {
                // exec itself failed (e.g. the container just stopped); keep its error readable.
                std::cout << "\nThe shell exited with code " << exit_code
                          << ". Press Enter to return to g-systemctl..." << std::flush;
                std::string line;
                std::getline(std::cin, line);
            }
        })();
        clear_messages();
        if (exit_code != 0 && quick)
        {
            error_message_ = "Shell in " + name + " failed (exit code " + std::to_string(exit_code) + ")";
            shells_.erase(id); // detect again next time
        }
        else
        {
            status_message_ = "Left " + shell + " in " + name;
        }
        refresh_now();
    }

    void ContainersView::fold_selected(bool collapse)
    {
        if (selected_index_ < 0 || selected_index_ >= static_cast<int>(rows_.size()))
            return;
        std::string project;
        if (const auto *p = selected_project())
            project = p->ref.name;
        else if (const auto *c = selected(); c && collapse)
            project = c->compose_project;
        if (project.empty())
            return;
        if (collapse)
        {
            collapsed_.insert(project);
            selected_key_ = kProjectKey + project;
        }
        else
        {
            collapsed_.erase(project);
        }
        apply_filter();
    }

    void ContainersView::open_project_detail(const std::string &name, bool logs_full)
    {
        if (!find_project(name))
            return;
        logs_full_ = logs_full;
        if (detail_open_ && detail_project_ == name)
            return;
        if (detail_open_)
            stop_logs();
        clear_messages();
        detail_open_ = true;
        summary_.reset();
        detail_project_ = name;
        logs_id_.clear();
        logs_name_ = name;
        details_ = ContainerDetails{};
        details_error_.clear();
        set_poll_project(name, false);
        refresh_now();
        logs_.open("compose " + name,
                   "Starting: " + manager_->compose_logs_command(name, kLogTail));
        attach_logs(kLogTail, "", false);
    }

    void ContainersView::open_detail(bool logs_full)
    {
        if (const auto *p = selected_project(); p && !detail_open_)
        {
            open_project_detail(p->ref.name, logs_full);
            return;
        }
        const auto *c = selected();
        if (!c)
            return;
        logs_full_ = logs_full;
        if (detail_open_ && logs_id_ == c->id)
            return;
        clear_messages();
        detail_open_ = true;
        detail_project_.clear();
        summary_.reset();
        logs_id_ = c->id;
        logs_name_ = c->name;
        details_ = ContainerDetails{};
        reload_details();
        if (c->is_running())
            detect_shell(c->id, false);
        logs_.open(c->name + " (" + c->id + ")",
                   "Starting: " + manager_->runtime_name() + " logs -f --tail " + std::to_string(kLogTail) + " " + c->name);
        attach_logs(kLogTail, "", false);
    }

    void ContainersView::close_detail()
    {
        shell_open_pending_.clear();
        stop_logs();
        detail_open_ = false;
        logs_full_ = false;
        detail_project_.clear();
        set_poll_project("", false);
        details_ = ContainerDetails{};
        details_error_.clear();
    }

    bool ContainersView::back()
    {
        if (logs_full_)
        {
            logs_full_ = false;
            return true;
        }
        if (detail_open_)
        {
            close_detail();
            clear_messages();
            return true;
        }
        return false;
    }

    void ContainersView::attach_logs(int tail, const std::string &since, bool resume)
    {
        const int generation = ++logs_generation_;
        logs_waiting_ = false;
        // Docker can report a container as running for a moment after its log
        // stream closed, so a resumed stream may end at once with no output.
        // Announce the restart only once output actually arrives.
        auto got_line = std::make_shared<std::atomic<bool>>(false);
        auto announce = resume ? "── " + logs_name_ + " started ──" : std::string();
        auto on_line = [this, got_line, announce](std::string line) {
            if (!got_line->exchange(true) && !announce.empty())
                logs_.append(announce);
            logs_.append(std::move(line));
        };
        auto on_end = [this, generation, got_line, resume] {
            auto ended_at = unix_time_now();
            bool quiet_retry = resume && !got_line->load();
            screen_.Post([this, generation, ended_at, quiet_retry] { on_logs_ended(generation, ended_at, quiet_retry); });
            screen_.PostEvent(Event::Custom);
        };
        std::unique_ptr<LogStream> stream;
        if (!detail_project_.empty())
        {
            const auto *p = find_project(detail_project_);
            stream = p ? manager_->stream_compose_logs(p->ref, p->container_ids, tail, since, on_line, on_end) : nullptr;
        }
        else
        {
            stream = manager_->stream_logs(logs_id_, tail, since, on_line, on_end);
        }
        if (!stream)
        {
            error_message_ = "Unable to start log stream";
        }
        logs_.attach(std::move(stream));
    }

    void ContainersView::stop_logs()
    {
        ++logs_generation_;
        logs_waiting_ = false;
        logs_id_.clear();
        logs_name_.clear();
        logs_.close();
    }

    void ContainersView::on_logs_ended(int generation, const std::string &ended_at, bool quiet_retry)
    {
        if (generation != logs_generation_ || !detail_open_)
            return;
        logs_waiting_ = true;
        if (quiet_retry)
        {
            // Keep the original end time so no lines are skipped; retry on the next poll.
            return;
        }
        logs_ended_at_ = ended_at;
        logs_.append("── " + logs_name_ + " is not running; logs resume when it starts ──");
        refresh_now();
    }

    void ContainersView::reattach_logs_if_running(const std::string &listed_at)
    {
        // A list fetched before the stream ended may still show the container running.
        if (!detail_open_ || !logs_waiting_ || std::stold(listed_at) <= std::stold(logs_ended_at_))
            return;
        if (!detail_project_.empty())
        {
            const auto *p = find_project(detail_project_);
            if (p && p->running > 0)
                attach_logs(-1, logs_ended_at_, true);
            return;
        }
        auto it = std::find_if(containers_.begin(), containers_.end(),
                               [this](const Container &c) { return c.id == logs_id_; });
        if (it == containers_.end())
        {
            // Recreated (e.g. `docker compose up`): same name, new ID.
            it = std::find_if(containers_.begin(), containers_.end(),
                              [this](const Container &c) { return c.name == logs_name_; });
            if (it == containers_.end())
                return;
            logs_id_ = it->id;
            logs_.set_title(it->name + " (" + it->id + ")");
            reload_details();
        }
        if (!it->is_running())
            return;
        detect_shell(it->id, false);
        attach_logs(-1, logs_ended_at_, true);
    }

    void ContainersView::reload_details()
    {
        if (logs_id_.empty())
            return;
        try
        {
            details_ = manager_->inspect(logs_id_);
            if (details_.id.size() > 12)
                details_.id.resize(12);
            details_error_.clear();
        }
        catch (const std::exception &e)
        {
            // Container may have been removed; keep the last snapshot.
            details_error_ = "Inspect failed: " + std::string(e.what());
        }
    }

    // ---- resource editor -------------------------------------------------------

    void ContainersView::open_edit_dialog()
    {
        const auto *c = action_target();
        if (!c)
        {
            report_removed();
            return;
        }
        try
        {
            auto details = manager_->inspect(c->id);
            edit_target_id_ = c->id;
            edit_target_name_ = c->name;
            edit_original_ = details.resource_settings();
            edit_values_ = edit_original_;
            const std::string *fields[] = {&edit_values_.cpus, &edit_values_.memory, &edit_values_.memory_swap,
                                           &edit_values_.pids_limit, &edit_values_.restart_policy};
            for (int i = 0; i < 5; ++i)
                edit_cursors_[i] = static_cast<int>(fields[i]->size());
            edit_error_.clear();
            edit_open_ = true;
            clear_messages();
        }
        catch (const std::exception &e)
        {
            error_message_ = "Inspect failed: " + std::string(e.what());
        }
    }

    void ContainersView::submit_edit_dialog()
    {
        auto result = build_resource_update(edit_original_, edit_values_);
        if (!result.error.empty())
        {
            edit_error_ = result.error;
            return;
        }
        auto id = edit_target_id_;
        auto name = edit_target_name_;
        close_edit_dialog();
        if (result.update.empty())
        {
            status_message_ = "No changes for " + name;
            return;
        }
        auto update = result.update;
        run_action("Updating " + name + "...", [this, id, update] { return manager_->update_resources(id, update); },
                   "Updated resources of " + name, id, {"updating", "updated", "update failed"});
    }

    void ContainersView::close_edit_dialog()
    {
        edit_open_ = false;
        edit_error_.clear();
        screen_.PostEvent(Event::Custom);
    }

    // ---- add compose project ----------------------------------------------------

    void ContainersView::open_add_dialog()
    {
        clear_messages();
        if (!compose_ok_)
        {
            error_message_ = compose_message_.empty() ? "compose is not available" : compose_message_;
            return;
        }
        add_path_.clear();
        add_cursor_ = 0;
        add_error_.clear();
        add_open_ = true;
    }

    void ContainersView::close_add_dialog()
    {
        add_open_ = false;
        add_error_.clear();
        screen_.PostEvent(Event::Custom);
    }

    void ContainersView::submit_add_dialog()
    {
        auto path = trim(add_path_);
        if (path.empty())
        {
            add_error_ = "Enter a compose file or the directory that holds one";
            return;
        }
        if (path[0] == '~')
        {
            const char *home = std::getenv("HOME");
            path = std::string(home ? home : "") + path.substr(1);
        }
        char resolved[PATH_MAX];
        if (!::realpath(path.c_str(), resolved))
        {
            add_error_ = "Not found: " + path;
            return;
        }
        path = resolved;
        struct stat st;
        ::stat(path.c_str(), &st);
        ComposeProjectRef ref;
        if (S_ISDIR(st.st_mode))
        {
            ref.working_dir = path;
        }
        else
        {
            ref.working_dir = path.substr(0, path.find_last_of('/'));
            ref.config_files = {path};
        }
        try
        {
            // Honors a top-level `name:`, and checks the file is valid.
            ref.name = manager_->compose_project_name(ref);
        }
        catch (const std::exception &e)
        {
            add_error_ = e.what();
            return;
        }
        const auto *existing = find_project(ref.name);
        if (existing && !existing->down())
        {
            close_add_dialog();
            status_message_ = ref.name + " is already listed";
        }
        else
        {
            compose_store_.remember(ref);
            close_add_dialog();
            if (!compose_store_.error().empty())
                error_message_ = compose_store_.error();
            else
                status_message_ = "Added compose project " + ref.name + " (Alt+o brings it up)";
        }
        collapsed_.erase(ref.name);
        selected_key_ = kProjectKey + ref.name;
        apply_filter();
    }

    Component ContainersView::add_project_dialog()
    {
        InputOption option;
        option.multiline = false;
        option.cursor_position = &add_cursor_;
        auto input = Input(&add_path_, "/path/to/compose.yaml or its directory", option);
        auto renderer = Renderer(input, [this, input] {
            Elements lines = {
                text(" Add compose project") | bold | color(AccentColor()),
                separator() | color(BorderColor()),
                hbox({text(" Path ") | color(HeaderColor()), text("[") | color(BorderColor()),
                      input->Render() | color(TextColor()) | flex, text("]") | color(BorderColor())}),
                text(" Projects with containers show up by themselves. Add one here to") | color(MutedColor()),
                text(" bring it up from g-systemctl, or keep it listed while it is down.") | color(MutedColor()),
            };
            if (!add_error_.empty())
                lines.push_back(paragraph(" " + add_error_ + " ") | color(Colors::error_fg()));
            lines.push_back(separator() | color(BorderColor()));
            lines.push_back(text(" Enter add  Esc cancel") | color(MutedColor()));
            return vbox(std::move(lines)) | size(WIDTH, GREATER_THAN, 66) | size(WIDTH, LESS_THAN, 90) |
                   borderStyled(ROUNDED, BorderColor()) | bgcolor(PanelColor()) | isolated_borders;
        });
        return CatchEvent(renderer, [this](Event event) {
            if (event == Event::Return)
            {
                submit_add_dialog();
                return true;
            }
            if (event == Event::Escape)
            {
                close_add_dialog();
                return true;
            }
            return false;
        });
    }

    Component ContainersView::edit_dialog()
    {
        auto make_input = [](std::string *value, const std::string &placeholder, int *cursor) {
            InputOption option;
            option.multiline = false;
            option.cursor_position = cursor;
            return Input(value, placeholder, option);
        };
        auto cpus = make_input(&edit_values_.cpus, "unlimited", &edit_cursors_[0]);
        auto memory = make_input(&edit_values_.memory, "unlimited", &edit_cursors_[1]);
        auto swap = make_input(&edit_values_.memory_swap, "default", &edit_cursors_[2]);
        auto pids = make_input(&edit_values_.pids_limit, "unlimited", &edit_cursors_[3]);
        auto restart = make_input(&edit_values_.restart_policy, "no", &edit_cursors_[4]);
        auto inputs = ftxui::Container::Vertical({cpus, memory, swap, pids, restart});

        auto row = [](const std::string &label, Component input, const std::string &hint) {
            return hbox({
                text(" " + label) | color(HeaderColor()) | size(WIDTH, EQUAL, 16),
                text("[") | color(BorderColor()),
                input->Render() | color(TextColor()) | size(WIDTH, EQUAL, 16),
                text("]") | color(BorderColor()),
                text(" " + hint) | color(MutedColor()),
            });
        };

        auto renderer = Renderer(inputs, [this, cpus, memory, swap, pids, restart, row] {
            Elements lines = {
                hbox({text(" Resources  ") | bold | color(AccentColor()), text(edit_target_name_) | color(WarningColor())}),
                separator() | color(BorderColor()),
                row("CPUs", cpus, "e.g. 0.5, 2"),
                row("Memory", memory, "e.g. 256m, 2g"),
                row("Memory+swap", swap, "-1 = unlimited swap"),
                row("PIDs limit", pids, "empty = unlimited"),
                row("Restart", restart, "no|always|unless-stopped|on-failure[:N]"),
                separator() | color(BorderColor()),
                text(" Applied live with `" + manager_->runtime_name() + " update`. Env, ports and volumes") |
                    color(MutedColor()),
                text(" need the container to be recreated.") | color(MutedColor()),
            };
            if (!edit_error_.empty())
            {
                lines.push_back(paragraph(" " + edit_error_ + " ") | color(Colors::error_fg()));
            }
            lines.push_back(separator() | color(BorderColor()));
            lines.push_back(text(" Tab/↑↓ field  Enter apply  Esc cancel") | color(MutedColor()));
            return vbox(std::move(lines)) | size(WIDTH, LESS_THAN, 80) | borderStyled(ROUNDED, BorderColor()) |
                   bgcolor(PanelColor()) | isolated_borders;
        });

        return CatchEvent(renderer, [this](Event event) {
            if (event == Event::Return)
            {
                submit_edit_dialog();
                return true;
            }
            if (event == Event::Escape)
            {
                close_edit_dialog();
                return true;
            }
            return false;
        });
    }

    // ---- events ---------------------------------------------------------------

    bool ContainersView::handle_event(Event event)
    {
        if (event == Event::Special("\x1b" "R"))
        {
            refresh_now();
            status_message_ = "Refreshing...";
            return true;
        }
        if (event == Event::Special("\x1b" "c"))
        {
            close_detail();
            clear_messages();
            return true;
        }
        if (detail_open_)
        {
            if (event == Event::Special("\x1b" "s"))
            {
                show_secrets_ = !show_secrets_;
                return true;
            }
            if (event == Event::PageUp || event == Event::PageDown)
            {
                logs_.scroll(event == Event::PageUp ? -10 : 10);
                return true;
            }
            // Arrows scroll the summary, or the logs when they fill the page.
            const bool up = event == Event::ArrowUp || event == Event::Special("\x1b" "k");
            if (up || event == Event::ArrowDown || event == Event::Special("\x1b" "j"))
            {
                if (logs_full_)
                    logs_.scroll(up ? -1 : 1);
                else
                    summary_.scroll(up ? -1 : 1);
                return true;
            }
        }
        else if (event == Event::ArrowUp || event == Event::Special("\x1b" "k") || event == Event::ArrowDown ||
                 event == Event::Special("\x1b" "j"))
        {
            const bool up = event == Event::ArrowUp || event == Event::Special("\x1b" "k");
            select(selected_index_ + (up ? -1 : 1), up ? -1 : 1);
            clear_messages();
            return true;
        }
        else if (event == Event::ArrowLeft || event == Event::ArrowRight)
        {
            fold_selected(event == Event::ArrowLeft);
            clear_messages();
            return true;
        }

        if (!available_)
        {
            return false;
        }

        // Actions are ignored while a previous one is still running.
        // Alt+s: sort by the next column; Alt+Shift+s: reverse (on the page, Alt+s shows secrets).
        if (!detail_open_ && (event == Event::Special("\x1b" "s") || event == Event::Special("\x1b" "S")))
        {
            if (event == Event::Special("\x1b" "S"))
            {
                sort_by(sort_.column);
                return true;
            }
            std::vector<SortColumn> cycle;
            for (const auto &c : kListColumns)
                if (c.sortable)
                    cycle.push_back(c.column);
            auto it = std::find(cycle.begin(), cycle.end(), sort_.column);
            sort_by(it == cycle.end() || it + 1 == cycle.end() ? cycle.front() : *(it + 1));
            return true;
        }

        const bool is_action = event == Event::Special("\x1b" "o") || event == Event::Special("\x1b" "x") ||
                               event == Event::Special("\x1b" "r") || event == Event::Special("\x1b" "u") ||
                               event == Event::Special("\x1b" "b") || event == Event::Special("\x1b" "p") ||
                               event == Event::Special("\x1b" "d");
        if (is_action && busy_)
            return true;

        // Enter drills down: list -> detail page -> full-size logs (and back).
        if (event == Event::Return || event == Event::Special("\x1b" "f"))
        {
            if (detail_open_)
            {
                logs_full_ = !logs_full_;
                // The log frame settles on the second layout pass after a size change.
                screen_.PostEvent(Event::Custom);
            }
            else if (event == Event::Return)
                open_detail(false);
            return true;
        }
        if (event == Event::Special("\x1b" "l"))
        {
            open_detail(true);
            return true;
        }
        if (event == Event::Special("\x1b" "i"))
        {
            open_detail(false);
            return true;
        }
        if (event == Event::Special("\x1b" "o"))
        {
            start_target();
            return true;
        }
        if (event == Event::Special("\x1b" "x"))
        {
            stop_target();
            return true;
        }
        if (event == Event::Special("\x1b" "r"))
        {
            restart_target();
            return true;
        }
        if (event == Event::Special("\x1b" "u"))
        {
            // On a compose project there are no limits to edit; Alt+u runs `up -d`.
            if (project_target())
                compose_up_target();
            else
                open_edit_dialog();
            return true;
        }
        if (event == Event::Special("\x1b" "b"))
        {
            compose_build_target();
            return true;
        }
        if (event == Event::Special("\x1b" "p"))
        {
            compose_pull_target();
            return true;
        }
        if (event == Event::Special("\x1b" "d"))
        {
            compose_down_target();
            return true;
        }
        if (event == Event::Special("\x1b" "t"))
        {
            shell_target();
            return true;
        }
        if (event == Event::Special("\x1b" "a"))
        {
            if (!detail_open_)
                open_add_dialog();
            return true;
        }
        if (event == Event::Special("\x1b" "v"))
        {
            // From a compose container to its project's page.
            const auto *c = action_target();
            if (c && !c->compose_project.empty())
                open_project_detail(c->compose_project, false);
            return true;
        }
        if (event.is_character() || event == Event::Backspace)
        {
            // The list is hidden on the detail page, so typing does not change the filter there.
            if (detail_open_)
                return true;
            if (event.is_character())
                filter_text_ += event.character();
            else if (!filter_text_.empty())
                filter_text_.pop_back();
            apply_filter(true);
            clear_messages();
            return true;
        }
        if (event.is_mouse())
        {
            auto &mouse = event.mouse();
            if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown)
            {
                int delta = mouse.button == Mouse::WheelUp ? -1 : 1;
                if (!detail_open_)
                    select(selected_index_ + delta, delta);
                else if (logs_.contains(mouse.x, mouse.y))
                    logs_.scroll(delta * 3);
                else if (summary_.contains(mouse.x, mouse.y))
                    summary_.scroll(delta * 3);
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed)
            {
                if (!detail_open_)
                {
                    for (size_t i = 0; i < header_boxes_.size() && i < kListColumns.size(); ++i)
                    {
                        if (kListColumns[i].sortable && header_boxes_[i].Contain(mouse.x, mouse.y))
                        {
                            sort_by(kListColumns[i].column);
                            return true;
                        }
                    }
                }
                switch (actions_.hit(mouse.x, mouse.y))
                {
                case ActionBar::Action::Start:
                    if (!busy_)
                        start_target();
                    return true;
                case ActionBar::Action::Stop:
                    if (!busy_)
                        stop_target();
                    return true;
                case ActionBar::Action::Restart:
                    if (!busy_)
                        restart_target();
                    return true;
                case ActionBar::Action::Back:
                    back();
                    return true;
                case ActionBar::Action::ToggleLogs:
                    logs_full_ = !logs_full_;
                    screen_.PostEvent(Event::Custom);
                    return true;
                case ActionBar::Action::ToggleGraphs:
                    if (graphs_)
                        graphs_->toggle();
                    return true;
                case ActionBar::Action::Shell:
                    shell_target();
                    return true;
                case ActionBar::Action::None:
                    break;
                }
                if (detail_open_)
                    return true;
                for (size_t i = 0; i < item_boxes_.size(); ++i)
                {
                    if (item_boxes_[i].Contain(mouse.x, mouse.y) && i < rows_.size() &&
                        rows_[i].kind != ListRow::Kind::Divider)
                    {
                        // Clicking the selected row opens it, like Enter.
                        if (static_cast<int>(i) == selected_index_)
                            open_detail(false);
                        else
                            select(static_cast<int>(i));
                        clear_messages();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ---- rendering -------------------------------------------------------------

    Element ContainersView::render_body()
    {
        actions_.hide();
        if (!checked_)
        {
            return text("Connecting to " + manager_->runtime_name() + "...") | color(MutedColor()) | center | flex;
        }
        if (!available_)
        {
            return vbox({
                       text(manager_->runtime_name() + " is not available") | bold | color(WarningColor()) | center,
                       text(""),
                       paragraph(availability_message_) | color(MutedColor()) | center,
                       text(""),
                       paragraph(manager_->setup_hint()) | color(MutedColor()) | center,
                   }) |
                   center | flex;
        }
        return detail_open_ ? render_detail() : render_list() | flex;
    }

    Element ContainersView::render_detail()
    {
        if (!detail_project_.empty())
            return render_project_detail();
        const auto *c = action_target();
        actions_.fit_title(logs_name_, c ? state_style(c->state).label : "✕ removed");
        Element actions = c ? actions_.render(state_style(c->state).label, state_style(c->state).fg, c->is_active())
                            : actions_.render("✕ removed", Colors::error_fg(), false, false);
        auto title = detail_title_bar(actions_.render_back(), logs_name_, c ? c->image : details_.image, std::move(actions));
        Element summary = logs_full_ ? text("") : summary_.render(render_summary());
        const bool graphs = graphs_ ? graphs_->shown() : graphs_fit();
        // The title bar is full at narrow widths; the shell button goes with the log buttons.
        actions_.fit_log_header(c != nullptr, logs_full_, graphs, graphs_fit());
        auto log_header = actions_.render_log_header(logs_full_, graphs, graphs_fit());
        if (c)
        {
            auto it = shells_.find(c->id);
            log_header = hbox({actions_.render_shell(c->is_running() && it != shells_.end() && it->second.ok),
                               std::move(log_header)});
        }
        return detail_page(std::move(title), std::move(summary), logs_.render(std::move(log_header)), logs_full_,
                           logs_full_ || !graphs ? nullptr : render_graphs());
    }

    Element ContainersView::render_list()
    {
        if (rows_.empty())
        {
            item_boxes_.clear();
            return text(containers_.empty() && projects_.empty() ? "No containers" : "No containers match the filter") |
                   center | dim;
        }

        // Inside the border: selection mark, trailing space and the scroll indicator.
        cols_ = fit_columns(Terminal::Size().dimx - 2 - 3);
        // The sorted column shows ▲ (ascending) or ▼; headers are clickable.
        header_boxes_.resize(kListColumns.size());
        for (auto &box : header_boxes_)
            box = Box{1, 0, 1, 0}; // empty: hidden columns take no clicks
        auto label = [this](size_t i) {
            const auto &c = kListColumns[i];
            if (!c.sortable || c.column != sort_.column)
                return c.header;
            return c.header + (sort_.descending ? " ▼" : " ▲");
        };
        auto hc = [this](size_t i) {
            return kListColumns[i].sortable && kListColumns[i].column == sort_.column ? AccentColor() : HeaderColor();
        };
        auto head = [&](size_t i, bool right) {
            auto e = text(label(i)) | color(hc(i));
            if (right)
                e = e | align_right;
            return e | reflect(header_boxes_[i]);
        };
        Elements header_cells = {text(" "), flex_cell(label(0), hc(0)) | reflect(header_boxes_[0])};
        for (auto &e : fixed_columns({head(1, false), head(2, false), head(3, false), head(4, true), head(5, true),
                                      head(6, true)}))
            header_cells.push_back(e);
        header_cells.push_back(text("  ")); // trailing space + the column rows lose to vscroll_indicator
        auto header = hbox(std::move(header_cells)) | bgcolor(PanelColor());

        // Projects in the compose group, containers in the standalone group.
        auto section_count = [this](int section) {
            int count = 0;
            bool inside = false;
            for (const auto &r : rows_)
            {
                if (r.kind == ListRow::Kind::Divider)
                    inside = r.index == section;
                else if (inside && r.kind == (section == kComposeSection ? ListRow::Kind::Project : ListRow::Kind::Container))
                    ++count;
            }
            return count;
        };

        Elements rows;
        item_boxes_.resize(rows_.size());
        for (size_t i = 0; i < rows_.size(); ++i)
        {
            const auto &r = rows_[i];
            const bool is_selected = static_cast<int>(i) == selected_index_;
            Element row;
            if (r.kind == ListRow::Kind::Divider)
            {
                const bool compose = r.index == kComposeSection;
                const auto fg = compose ? AccentColor() : StandaloneColor();
                auto count = section_count(r.index);
                auto noun = compose ? (count == 1 ? " project" : " projects")
                                    : (count == 1 ? " container" : " containers");
                row = hbox({
                    text(" ──") | color(BorderColor()),
                    text(compose ? " COMPOSE " : " STANDALONE ") | bold | color(fg),
                    text(std::to_string(count) + noun + " ") | color(MutedColor()),
                    separatorCharacter("─") | color(BorderColor()) | flex,
                });
                rows.push_back(row | reflect(item_boxes_[i]));
                continue;
            }
            if (r.kind == ListRow::Kind::Project)
            {
                row = render_project_row(projects_[r.index], is_selected);
            }
            else
            {
                const auto &c = filtered_[r.index];
                row = render_container_row(c, is_selected, !c.compose_project.empty(), r.last_child);
            }
            if (is_selected)
            {
                row = hbox({text("▌") | color(AccentColor()), row | flex}) | bgcolor(Colors::selected_bg()) | focus;
            }
            else
            {
                row = hbox({text(" "), row | flex});
            }
            rows.push_back(row | reflect(item_boxes_[i]));
        }

        return vbox({
                   header,
                   vbox(std::move(rows)) | vscroll_indicator | yframe | flex,
               }) |
               flex;
    }

    ContainersView::ListColumns ContainersView::fit_columns(int room)
    {
        // Steps in order until the name column gets kMinNameWidth.
        ListColumns c;
        const std::vector<std::function<void(ListColumns &)>> steps = {
            [](ListColumns &c) { c.image = 16; },
            [](ListColumns &c) { c.status = 16; },
            [](ListColumns &c) { c.image = 0; },
            [](ListColumns &c) { c.pids = 0; },
            [](ListColumns &c) { c.mem = 15; },   // "214MiB / 1GiB"
            [](ListColumns &c) { c.status = 0; },
            [](ListColumns &c) { c.mem = 9; },    // "999.9 MiB" (projects); containers cut at the left
        };
        for (const auto &step : steps)
        {
            if (room - c.total() >= kMinNameWidth)
                break;
            step(c);
        }
        return c;
    }

    Elements ContainersView::fixed_columns(std::vector<Element> cells) const
    {
        // A column starts with a space, so neighbours never touch when a value fills it.
        const int widths[] = {cols_.image, cols_.state, cols_.status, cols_.cpu, cols_.mem, cols_.pids};
        Elements out;
        for (size_t i = 0; i < cells.size() && i < 6; ++i)
        {
            if (widths[i] <= 0)
                continue;
            out.push_back(hbox({text(" "), cells[i] | flex}) | size(WIDTH, EQUAL, widths[i]));
        }
        return out;
    }

    Element ContainersView::render_project_row(const ComposeProject &p, bool is_selected)
    {
        const auto &name = p.ref.name;
        const bool folded = collapsed_.count(name) && filter_text_.empty();
        auto style = project_style(p);
        auto totals = project_totals(containers_, name);
        auto sampled = [&totals](const std::string &value) { return totals.sampled ? value : std::string("-"); };

        std::string kind = p.down() ? "compose · saved"
                                    : "compose · " + std::to_string(p.services.size()) +
                                          (p.services.size() == 1 ? " service" : " services");
        const int status_room = std::max(1, cols_.status - 1);
        Element status = text("");
        if (auto it = drift_.find(name); it != drift_.end() && !it->second.empty())
            status = text(ellipsize("⚠ " + std::to_string(it->second.size()) + " changed (Alt+u)", status_room)) | color(WarningColor());
        else if (drift_error_.count(name))
            status = text(ellipsize("? config unreadable", status_room)) | color(MutedColor());
        else if (p.down())
            status = text(ellipsize("Alt+o to bring up", status_room)) | color(MutedColor());
        if (auto activity = row_activity_.render(kProjectKey + name))
            status = activity;

        Elements row = {
            text(p.down() ? "▹ " : folded ? "▸ " : "▾ ") | color(AccentColor()),
            text(name) | bold | color(is_selected ? Color::White : AccentColor()) | flex,
        };
        auto state = text(style.label) | color(style.fg);
        if (p.running > 0)
            state = state | bold;
        for (auto &e : fixed_columns({text(ellipsize(kind, std::max(1, cols_.image - 1))) | color(MutedColor()), state, status,
                                      text(sampled(format_metric_percent(totals.cpu))) | color(MutedColor()) | align_right,
                                      text(sampled(format_metric_bytes(totals.memory))) | color(MutedColor()) | align_right,
                                      text(sampled(format_metric_count(totals.pids))) | color(MutedColor()) | align_right}))
            row.push_back(e);
        row.push_back(text(" "));
        return hbox(std::move(row));
    }

    Element ContainersView::render_container_row(const Container &c, bool is_selected, bool child, bool last_child)
    {
        auto style = state_style(c.state);
        auto dash = [](const std::string &v) { return v.empty() ? std::string("-") : v; };
        Element name;
        if (child)
        {
            const bool multi = multi_replica_.count(c.compose_project + "/" + c.compose_service) > 0;
            bool drifted = false;
            if (auto it = drift_.find(c.compose_project); it != drift_.end())
                drifted = std::find(it->second.begin(), it->second.end(), c.compose_service) != it->second.end();
            name = hbox({
                text(last_child ? "  └ " : "  ├ ") | color(MutedColor()),
                text(service_label(c, multi)) | color(is_selected ? Color::White : TextColor()),
                drifted ? text(" ⚠") | color(WarningColor()) : text(""),
                filler(),
            });
            if (is_selected)
                name = name | bold;
        }
        else
        {
            name = flex_cell(c.name, is_selected ? Color::White : TextColor(), is_selected);
        }
        // An action on the container, else on its compose project.
        auto status = row_activity_.render(c.id);
        if (!status && child)
            status = row_activity_.render(kProjectKey + c.compose_project);
        if (!status)
            status = text(ellipsize(c.status, std::max(1, cols_.status - 1))) | color(MutedColor());
        Elements row = {name | flex};
        auto state = text(style.label) | color(style.fg);
        if (c.is_running())
            state = state | bold;
        for (auto &e : fixed_columns({text(ellipsize(c.image, std::max(1, cols_.image - 1))) | color(MutedColor()), state, status,
                                      text(dash(c.stats.cpu)) | color(MutedColor()) | align_right,
                                      text(dash(c.stats.mem_usage)) | color(MutedColor()) | align_right,
                                      text(dash(c.stats.pids)) | color(MutedColor()) | align_right}))
            row.push_back(e);
        row.push_back(text(" "));
        return hbox(std::move(row));
    }

    Elements ContainersView::render_summary()
    {
        const auto &d = details_;
        const auto *c = action_target();
        auto settings = d.resource_settings();
        Elements lines;
        auto section = [&lines](const std::string &title) { lines.push_back(section_title(title)); };

        if (!details_error_.empty())
            lines.push_back(paragraph(" " + details_error_) | color(Colors::error_fg()));

        lines.push_back(section_title("Overview", true));
        if (c)
        {
            auto style = state_style(c->state);
            lines.push_back(field_row("State", style.label + " · " + c->status, style.fg));
            if (c->is_running() && !c->stats.cpu.empty())
            {
                lines.push_back(field_row("CPU", c->stats.cpu));
                lines.push_back(field_row("Memory", c->stats.mem_usage + "  (" + c->stats.mem_percent + ")"));
                lines.push_back(field_row("PIDs", c->stats.pids));
                lines.push_back(field_row("Net I/O", c->stats.net_io));
                lines.push_back(field_row("Block I/O", c->stats.block_io));
            }
            if (c->is_running())
            {
                auto it = shells_.find(c->id);
                if (it == shells_.end() || it->second.checking)
                    lines.push_back(field_row("Shell", "checking...", MutedColor()));
                else if (it->second.ok)
                    lines.push_back(field_row("Shell", it->second.shell + "  (Alt+t opens a terminal)", AccentColor()));
                else
                    lines.push_back(field_row("Shell", "unavailable: " + it->second.reason, MutedColor()));
            }
        }
        lines.push_back(field_row("ID", d.id));
        lines.push_back(field_row("Image", d.image));
        {
            // Both are JSON arrays from `docker inspect`; fall back to plain text.
            std::vector<std::string> tokens;
            for (const auto *part : {&d.entrypoint, &d.command})
            {
                auto parsed = parse_json_string_array(*part);
                auto add = parsed ? *parsed : split_command(*part);
                tokens.insert(tokens.end(), add.begin(), add.end());
            }
            lines.push_back(command_row("Command", tokens));
        }
        lines.push_back(field_row("Started", d.started_at + (d.restart_count.empty() ? "" : " · restarts " + d.restart_count)));
        lines.push_back(field_row("Created", d.created));
        if (c && !c->compose_project.empty())
        {
            const bool multi = multi_replica_.count(c->compose_project + "/" + c->compose_service) > 0;
            lines.push_back(field_row("Compose", c->compose_project + " › " + service_label(*c, multi) +
                                                     "   (Alt+v opens the project)"));
            if (!c->compose_depends_on.empty())
                lines.push_back(field_row("Depends on", join(c->compose_depends_on, ", ")));
        }
        else if (!d.compose_project.empty())
        {
            lines.push_back(field_row("Compose", d.compose_project + (d.compose_dir.empty() ? "" : "  (" + d.compose_dir + ")")));
        }

        section("Resources (Alt+u to edit)");
        lines.push_back(field_row("CPUs", limit_text(settings.cpus)));
        lines.push_back(field_row("Memory limit", d.memory > 0 ? format_bytes(d.memory) : "unlimited"));
        lines.push_back(field_row("Memory+swap", d.memory_swap < 0 ? "unlimited" : d.memory_swap == 0 ? "default" : format_bytes(d.memory_swap)));
        lines.push_back(field_row("PIDs limit", limit_text(settings.pids_limit)));
        lines.push_back(field_row("Restart", settings.restart_policy));

        section("Network");
        for (const auto &p : d.ports)
            lines.push_back(field_row("Port", p));
        for (const auto &n : d.networks)
            lines.push_back(field_row("Network", n));
        if (d.ports.empty() && d.networks.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section("Mounts");
        for (const auto &m : d.mounts)
            lines.push_back(hbox({text(" "), wrap_text_element(m) | flex}) | color(TextColor()));
        if (d.mounts.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section(std::string("Environment (read-only; recreate to change)  Alt+s ") + (show_secrets_ ? "hide" : "show") + " secrets");
        for (const auto &e : d.env)
        {
            std::string shown = e;
            if (!show_secrets_ && looks_secret(e))
            {
                shown = e.substr(0, e.find('=')) + "=••••••";
            }
            lines.push_back(hbox({text(" "), wrap_text_element(shown) | flex}) | color(TextColor()));
        }
        if (d.env.empty())
            lines.push_back(text(" none") | color(MutedColor()));
        return lines;
    }

    // ---- compose project page -----------------------------------------------------

    Element ContainersView::render_project_detail()
    {
        const auto *p = find_project(detail_project_);
        Element actions;
        if (p)
        {
            auto style = project_style(*p);
            actions_.fit_title(detail_project_, style.label);
            actions = actions_.render(style.label, style.fg, p->down() || p->active < p->total, p->active > 0);
        }
        else
        {
            actions_.fit_title(detail_project_, "✕ gone");
            actions = actions_.render("✕ gone", Colors::error_fg(), false, false);
        }
        auto title = detail_title_bar(actions_.render_back(), detail_project_, p ? p->ref.working_dir : "", std::move(actions));
        Element summary = text("");
        if (!logs_full_)
            summary = summary_.render(p ? render_project_summary(*p) : Elements{text(" Project no longer listed") | dim});
        const bool graphs = graphs_ ? graphs_->shown() : graphs_fit();
        return detail_page(std::move(title), std::move(summary),
                           logs_.render([&] {
                               actions_.fit_log_header(false, logs_full_, graphs, graphs_fit());
                               return actions_.render_log_header(logs_full_, graphs, graphs_fit());
                           }()),
                           logs_full_,
                           logs_full_ || !graphs ? nullptr : render_project_graphs());
    }

    Elements ContainersView::render_project_summary(const ComposeProject &p)
    {
        const auto &name = p.ref.name;
        Elements lines;
        auto section = [&lines](const std::string &title) { lines.push_back(section_title(title)); };

        lines.push_back(section_title("Overview", true));
        auto style = project_style(p);
        lines.push_back(field_row("State", p.down() ? "down · no containers" : style.label + " (" +
                                                                                  std::to_string(p.services.size()) +
                                                                                  (p.services.size() == 1 ? " service)" : " services)"),
                                  style.fg));
        lines.push_back(field_row("Directory", p.ref.working_dir));
        if (p.ref.config_files.empty())
            lines.push_back(field_row("Files", "default (compose.yaml in the directory)"));
        for (size_t i = 0; i < p.ref.config_files.size(); ++i)
            lines.push_back(field_row(i == 0 ? "Files" : "", p.ref.config_files[i]));
        auto totals = project_totals(containers_, name);
        if (totals.sampled)
            lines.push_back(field_row("Usage", "CPU " + format_metric_percent(totals.cpu) + " · " +
                                                   format_metric_bytes(totals.memory) + " · " +
                                                   format_metric_count(totals.pids) + " PIDs"));
        lines.push_back(field_row("Compose", compose_ok_ ? compose_message_ : "unavailable: " + compose_message_,
                                  compose_ok_ ? TextColor() : Colors::error_fg()));

        const auto drift = drift_.find(name);
        if (auto err = drift_error_.find(name); err != drift_error_.end())
            lines.push_back(field_row("Config", "cannot read the compose files: " + err->second, MutedColor()));
        else if (drift != drift_.end() && !drift->second.empty())
            lines.push_back(field_row("Config", "⚠ changed since created: " + join(drift->second, ", ") +
                                                    "  (Alt+u recreates)",
                                      WarningColor()));
        else if (drift != drift_.end() && !p.down())
            lines.push_back(field_row("Config", "✓ containers match the compose files", AccentColor()));
        else if (!p.down() && compose_ok_ && !drift_supported_)
            lines.push_back(field_row("Config", "not checked: " + compose_message_ + " has no `config --hash`",
                                      MutedColor()));
        else if (!p.down())
            lines.push_back(field_row("Config", "checking...", MutedColor()));

        section("Services");
        std::map<std::string, std::vector<std::string>> depends;
        for (const auto &id : p.container_ids)
        {
            const auto *c = find_container(id);
            if (!c)
                continue;
            const bool multi = multi_replica_.count(name + "/" + c->compose_service) > 0;
            const bool drifted = drift != drift_.end() && std::find(drift->second.begin(), drift->second.end(),
                                                                    c->compose_service) != drift->second.end();
            auto cs = state_style(c->state);
            // Same columns on every row: the drift mark sits after the name (as in
            // the list), and the status column is the one that gives way.
            lines.push_back(hbox({
                text(" "),
                hbox({text(service_label(*c, multi)) | bold | color(TextColor()),
                      drifted ? text(" ⚠") | color(WarningColor()) : text("")}) |
                    size(WIDTH, EQUAL, 15),
                cell(" " + cs.label, 14, cs.fg),
                text(" " + c->status) | color(MutedColor()) | size(WIDTH, EQUAL, 25) | xflex_shrink,
                elastic(text(" " + short_ports(c->ports)) | color(MutedColor())),
            }));
            if (!c->compose_oneoff && !c->compose_depends_on.empty())
                depends[c->compose_service] = c->compose_depends_on;
        }
        if (p.container_ids.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section("Dependencies");
        for (const auto &[service, deps] : depends)
            lines.push_back(field_row(service, "→ " + join(deps, ", ")));
        if (depends.empty())
            lines.push_back(text(" none") | color(MutedColor()));

        section("Actions");
        lines.push_back(field_row("Alt+o / x / r", p.down() ? "up -d / stop / restart" : "start / stop / restart"));
        lines.push_back(field_row("Alt+u", "up -d (apply config and image changes)"));
        lines.push_back(field_row("Alt+b", "up -d --build"));
        lines.push_back(field_row("Alt+p", "pull images"));
        lines.push_back(field_row("Alt+d", p.down() ? "forget (remove from this list)"
                                                    : "down (remove containers and networks)"));
        return lines;
    }

    Element ContainersView::render_project_graphs()
    {
        auto it = project_metrics_.find(detail_project_);
        if (it == project_metrics_.end())
            return nullptr;
        const auto &m = it->second;
        auto rate = [](double v) { return v < 0 ? std::string("-") : format_metric_rate(v); };
        std::vector<MetricView> views = {
            {"CPU", format_metric_percent(m.cpu.last()), &m.cpu, format_metric_percent, 1.0,
             graph_colors::Cpu()},
            {"Memory", format_metric_bytes(m.memory.last()), &m.memory, format_metric_bytes, 1024.0 * 1024.0,
             graph_colors::Memory()},
            {"PIDs", format_metric_count(m.pids.last()), &m.pids, format_metric_count, 1.0, graph_colors::Tasks(), false},
            {"Net I/O", "↓ " + rate(m.net_rx_per_s) + "  ↑ " + rate(m.net_tx_per_s), &m.net, format_metric_rate,
             1024.0, graph_colors::Network()},
            {"Disk I/O", "R " + rate(m.read_per_s) + "  W " + rate(m.write_per_s), &m.block, format_metric_rate,
             1024.0, graph_colors::Disk()},
        };
        return graph_panel_.render(views);
    }

} // namespace gsystemctl::ui
