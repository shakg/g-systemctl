---
title: "Following Docker logs across restarts and recreates"
description: "docker logs -f exits when a container stops. Here is how g-systemctl reattaches without losing or repeating lines, even after a compose recreate."
date: 2026-11-04
tags: [docker, logs, internals]
---

`docker logs -f` follows a container's output until the container stops. Then it exits. If you restart the container, you run the command again. If `docker compose up` recreates it, the container has a new ID and the old command would not find it anyway.

g-systemctl's container detail page has a log strip that is supposed to keep following. This post covers how it does that.

## The naive version and why it fails

The obvious fix is a loop: when `docker logs -f` exits, run it again. That has three problems.

1. **Repeated lines.** Without a start point, `docker logs` prints the whole log again, or the last `--tail` lines, which you have already seen.
2. **Busy loop.** While the container is stopped, the command exits right away, so the loop spins.
3. **Wrong container.** After a recreate, the old ID is gone and the loop fails forever.

## Remember when the stream ended

When the log process exits, g-systemctl records the time as a Unix timestamp with nanoseconds, the format `docker logs --since` takes:

```cpp
// Unix timestamp with nanoseconds, the format `docker logs --since` takes.
auto ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
snprintf(buf, sizeof(buf), "%lld.%09lld", ns / 1000000000, ns % 1000000000);
```

It then prints a marker in the log strip:

```text
── api is not running; logs resume when it starts ──
```

and waits. It does not restart the log process yet.

## Let the poller decide

The Containers tab already runs `docker ps` every 2 seconds. After each poll, the view checks whether the container it was following is running again. If it is, it starts a new stream:

```bash
docker logs -f --since 1793785333.104882113 <id>
```

`--since` starts exactly where the old stream stopped, so no line is shown twice and none is skipped.

Using the existing poll instead of a retry loop solves the busy loop for free: there is at most one check every 2 seconds, and it costs nothing extra because the list was being fetched anyway.

## A race with docker ps

There is a subtle ordering problem. The poll and the log stream run on different threads. A `docker ps` that started *before* the log stream ended can still report the container as running. Acting on that result would start a stream for a container that is already stopping.

The fix is to timestamp each poll before it runs, and ignore any poll that started before the stream ended:

```cpp
// A list fetched before the stream ended may still show the container running.
if (std::stold(listed_at) <= std::stold(logs_ended_at_))
    return;
```

Docker can also report a container as running for a moment after its log stream closed. A resumed stream may then end at once with no output. In that case g-systemctl keeps the *original* end time and quietly retries on the next poll, so no lines are lost. The `── api started ──` marker is printed only when the first line actually arrives.

## Recreated containers

`docker compose up` after a config change removes the container and creates a new one with the same name and a new ID. When the followed ID disappears from `docker ps`, g-systemctl looks for a container with the same name. If it finds one, it switches to the new ID, reloads the summary from `docker inspect` and attaches with the same `--since` timestamp.

From the user's side, the log strip just keeps going:

```text
09:42:11 info GET /v1/orders 200 12ms
09:42:13 ── api is not running; logs resume when it starts ──
09:42:16 ── api started ──
09:42:16 info listening on :8080
```

## Services do not need this

`journalctl -u <unit> -f` follows the journal, not the process. It keeps running across service restarts, so the service log strip needs none of the above.

Try it: open the Containers tab with `Tab` or `g-systemctl --containers`, press `Enter` on a container, then restart it from another terminal.
