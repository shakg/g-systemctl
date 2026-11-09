---
title: "Live CPU, memory and IO graphs for systemd services"
description: "How g-systemctl samples systemd counters every 2 seconds, turns them into rates and draws them with Unicode block characters."
date: 2026-11-05
tags: [systemd, graphs, internals]
---

Open a service in g-systemctl and the right half of the detail page shows four graphs: CPU, memory, tasks and IO. This post covers where the numbers come from and how they are drawn in a terminal. No agent or extra daemon is involved, just `systemctl` and some arithmetic.

## Where the numbers come from

systemd already tracks resource use per unit through cgroups. One command reads all of it:

```bash
systemctl show nginx.service --no-pager \
  -p CPUUsageNSec,MemoryCurrent,TasksCurrent,IOReadBytes,IOWriteBytes
```

```text
CPUUsageNSec=2481339000
MemoryCurrent=13002752
TasksCurrent=5
IOReadBytes=1843200
IOWriteBytes=40960
```

While a detail page is open, a sampler thread runs this every 2 seconds. It also refreshes the summary rows (state, PID, restart count) in the same pass. The thread hands the result to the UI thread with `ScreenInteractive::Post`, so rendering never waits on a subprocess.

Properties that systemd cannot report, such as IO accounting turned off for the unit, come back as `[not set]` or an empty value. The parser skips them and the graph for that metric is not shown. On macOS, `launchctl` has no equivalent, so the detail page shows the summary without graphs.

## Counters are not rates

`MemoryCurrent` and `TasksCurrent` are gauges: the value is the current value, so the graph plots it as is. `CPUUsageNSec` and the IO byte counts are totals since the unit started. Plotting them directly gives a line that only goes up.

The fix is to plot the change per second between two samples:

```cpp
// Rate of a growing counter (bytes, CPU nanoseconds). Ignores resets.
double update(double counter, double seconds) {
    double rate = -1.0;
    if (has_last_ && seconds > last_seconds_ && counter >= last_counter_)
        rate = (counter - last_counter_) / (seconds - last_seconds_);
    last_counter_ = counter;
    last_seconds_ = seconds;
    has_last_ = true;
    return rate;
}
```

Three details matter:

- **The time comes from a monotonic clock**, not from the 2-second schedule. If a sample is late because the machine is busy, the rate stays correct.
- **A counter that goes down means the service restarted** and its cgroup was recreated. That interval is dropped instead of plotted as a negative spike.
- **The first sample has nothing to compare against**, so the graph shows `collecting…` until the second one arrives.

CPU nanoseconds per second divided by 10⁷ gives percent of one core. A service using two full cores reads 200%.

## Ten minutes of history

Each metric keeps its last 300 samples in a `std::deque`. At one sample per 2 seconds that is 10 minutes, which is enough to see a restart, a memory leak or a cron job, and small enough that memory use never matters.

## Drawing with eighths

A terminal cell is too coarse for a smooth graph, but Unicode has eight block characters that fill a cell from the bottom: `▁▂▃▄▅▆▇█`. Each column of the graph is one sample, newest on the right. A value maps to a height in eighths of a cell: full cells are `█`, and the top cell gets the partial block.

```text
 CPU      3.1%                        max 4.2%
 ▂▃▂▂▃▅▄▃▂▂▃▃▂▁▂▃▆█▆▃▂▂▃▂▂▂▃▄▃▂▂▃▂▂▃▂▂▃▄▃▂▁▂
```

A sample above zero always gets at least `▁`, so an idle-but-alive service never looks flat-lined.

## Two kinds of scale

Rates such as CPU and IO are scaled from zero to the largest visible value, with a minimum range so that noise at 0.1% does not fill the graph.

Gauges need different handling. A database at 186 MiB that moves between 180 and 192 MiB would be a flat bar if scaled from zero. For memory and tasks the graph zooms in on the visible range instead, and the header shows `min – max` instead of `max`. Small changes on a large base stay visible.

## Fitting the terminal

At 100 or more columns, graphs sit beside the summary and are on by default. On narrower terminals they replace the summary, and `Alt+g` switches between the two. If a short terminal has fewer than two rows per metric, each metric drops to one line: label, value and a one-row graph. Nothing is cut off.

## Containers

The Containers tab already polls `docker stats` every 2 seconds for the list, so it keeps a history per running container from that same poll. When you open a container's detail page, its graphs already show the last few minutes. Network and block IO come from `docker stats` as totals since start, and use the same counter-to-rate step as above.

Try it: install from the [latest release](https://github.com/shakg/g-systemctl/releases/latest), run `sudo g-systemctl`, pick a service and press `Enter`.
