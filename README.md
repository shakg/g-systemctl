# G-Systemctl

![Test Status](https://github.com/shakg/g-systemctl/actions/workflows/test.yml/badge.svg)
![Build Status](https://github.com/shakg/g-systemctl/actions/workflows/release.yml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **New:** g-systemctl now manages Docker containers too. Open the Containers tab to see live CPU/memory, start/stop/restart containers, follow their logs, and change resource limits in place.

g-systemctl is a terminal user interface (TUI) for managing system services on Linux and macOS, and Docker containers. View, search, filter, and control your services and containers directly from the terminal.

## See It In Action

https://github.com/user-attachments/assets/1608dc3a-20aa-46fd-b3ea-db577f4c197c



## Features

- List all system services with status indicators
- Filter services by name in real-time
- Start, stop and restart services and containers with explicit shortcuts or buttons; stop and restart ask for confirmation
- Cross-platform support (Linux via systemctl, macOS via launchctl)
- Keyboard-driven navigation
- Mouse support (scroll to navigate, click to select)
- Initial filter via command line argument (`-f` / `--filter`)
- Stream service logs in the TUI via `journalctl`
- Containers tab for Docker: list with live CPU/memory/PIDs, start/stop/restart, follow logs, inspect env/ports/mounts, and change resource limits and restart policy in place (`docker update`)
- Docker Compose: containers grouped by project, with per-project start/stop/restart, `up -d`, `up --build`, `pull` and `down`, merged logs, summed graphs, and a warning when a service's compose file changed since its container was created

## Prerequisites

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+)
- Linux with systemd or macOS
- `journalctl` for Linux service log streaming
- Optional: the `docker` CLI and a reachable daemon for the Containers tab (Linux or Docker Desktop on macOS)

## Installation

### Debian / Ubuntu

For Ubuntu 22.04+ or Debian 12+. Download the `.deb` package from the latest release and install it.

amd64:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-amd64.deb
sudo apt install ./g-systemctl-linux-amd64.deb
```

arm64 (Raspberry Pi OS 64-bit, AWS Graviton, and similar):

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-arm64.deb
sudo apt install ./g-systemctl-linux-arm64.deb
```

### Linux Binary

For x86_64 or aarch64 with glibc 2.34+ (Ubuntu 22.04+, Debian 12+, Fedora 35+, and similar). The archive name uses `uname -m`:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-$(uname -m).tar.gz
tar -xzf g-systemctl-linux-$(uname -m).tar.gz
sudo install -m 755 g-systemctl-*/g-systemctl /usr/local/bin/g-systemctl
```

### macOS (Homebrew)

Download the formula from the latest release and install it locally:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl.rb
brew install ./g-systemctl.rb
```

### Build from source

```bash
git clone https://github.com/shakg/g-systemctl.git
cd g-systemctl
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

### Release Checksums

Run in the directory holding the downloaded file:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/SHA256SUMS.txt
sha256sum --ignore-missing -c SHA256SUMS.txt
```

## Usage

```bash
g-systemctl
```

For full functionality (start/stop services), run with sudo:

```bash
sudo g-systemctl
```

### Detail page

`Enter` on a service or container opens its detail page: a summary on top and a short live log strip below. `Enter` again expands the logs to the full page, and `Esc` goes back one level (full logs → detail page → list).

- Services: state and since when, unit file and enablement, main PID, tasks, memory, CPU time, restart policy and count, user, command, `WantedBy` (`systemctl show`; `launchctl list <label>` on macOS), with logs from `journalctl -f`.
- Containers: state, live CPU/memory/PIDs/IO, image, command, resource limits, ports, networks, mounts and environment, with logs from `docker logs -f --tail 200`. Logs keep following across restarts and recreates with the same name.

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Up` / `Down`, `Alt+k` / `Alt+j` | Move selection; on the detail page, scroll the summary (or the logs when expanded) |
| Type / `Backspace` | Filter the list |
| `Enter` | Open the detail page; on it, expand / shrink the logs |
| `Alt+f` | Expand / shrink the logs on the detail page |
| `Alt+l` | Open the detail page with the logs expanded |
| `Page Up` / `Page Down` | Scroll the logs |
| `Alt+o` | Start |
| `Alt+x` | Stop (asks for confirmation) |
| `Alt+r` | Restart (asks for confirmation) |
| `Esc` | Back one level: help, full logs, detail page, then clear the filter, then quit |
| `Alt+c` | Close the detail page |
| `Alt+q` | Quit |
| `?` | Show/hide help; `Tab`, `←`/`→` or `1`-`4` switch its tabs (Containers, Services, General, Settings) |
| `Tab` / `Alt+1` / `Alt+2` | Switch tab (`Alt+1` Containers, `Alt+2` Services) |

Services tab only:

| Key | Action |
|-----|--------|
| `Alt+e` | Edit the unit file in vim (`systemctl edit --full`); set `GSYSTEMCTL_EDITOR` to use another editor |
| `Alt+Shift+r` | Reload systemd (`systemctl daemon-reload`) |

### Containers Tab

| Key | Action |
|-----|--------|
| `Alt+i` | Open the detail page (same as `Enter`) |
| `Alt+u` | Edit CPUs, memory, memory+swap, PIDs limit and restart policy (`docker update`) |
| `Alt+s` | Show/hide env values whose names look secret (PASSWORD, TOKEN, KEY, ...) |
| `Alt+Shift+r` | Refresh now (the list refreshes every 2 seconds while the tab is open) |

Type to filter by container name, image, Compose project or service.

### Docker Compose

Containers created by Compose are grouped under their project, shown by service name (`worker #2` for replicas, `api (run)` for `compose run`). The project row shows how many containers are up and their summed CPU, memory and PIDs. `⚠ N changed` means a service's compose file changed since its container was created; `Alt+u` recreates it.

`Enter` on a project opens its page: directory, compose files, services, dependencies, summed graphs and merged logs (`docker compose logs -f`).

| Key (on a project row or page) | Action |
|-----|--------|
| `←` / `→` | Fold / unfold the project |
| `Alt+o` | `compose start`; `compose up -d` if the project is down |
| `Alt+x` / `Alt+r` | `compose stop` / `compose restart` (asks y/n) |
| `Alt+u` | `compose up -d`: apply config and image changes (asks y/n) |
| `Alt+b` | `compose up -d --build` (asks y/n) |
| `Alt+p` | `compose pull` |
| `Alt+d` | `compose down` (asks y/n); on a project that is already down, remove it from the list |
| `Alt+a` | Add a project by the path of its compose file or directory |
| `Alt+v` | From a container's page to its project's page |

Compose commands run from the project's directory with the compose files recorded in its container labels. Projects seen once are saved in `~/.config/g-systemctl/compose-projects` (or `$XDG_CONFIG_HOME`), so they stay listed after `compose down` and can be brought up again. `docker compose` is used if available, else `docker-compose`; set `GSYSTEMCTL_COMPOSE` (e.g. `podman compose`) to override.

Environment variables, ports and volumes cannot be changed on an existing container; Docker requires recreating it. Docker also cannot remove a CPU or memory limit in place, so clearing those fields is rejected. Clearing PIDs limit or memory+swap sets them to unlimited.

If your user cannot reach the Docker socket, add it to the `docker` group or run g-systemctl with sudo. Set `GSYSTEMCTL_DOCKER` to use a different CLI binary.

### Safe actions

`Enter` only navigates; it never changes a service or container. Starting is `Alt+o`. Stopping (`Alt+x`) and restarting (`Alt+r`) open a prompt that runs only on `y` or a click on its confirm button; `Enter` is ignored there, and `n` or `Esc` cancels. When sudo needs a password, the password prompt replaces the confirmation. On the detail page, actions apply to the service or container shown, and its title bar shows the current state.

### Mouse Support

| Action | Effect |
|--------|--------|
| Scroll up/down | Navigate the list; on the detail page, scroll the summary or logs under the pointer |
| Left click a row | Select it; click the selected row again to open its detail page |
| Left click `Start` / `Stop` / `Restart` / `Back` | Buttons in the detail page title bar |
| Left click `Expand` / `Shrink` | Resize the logs on the detail page |
| Left click `Reload(Alt+R)` | Reload systemd |
| Left click `Shortcuts(?)` | Show help |
| Left click `Services` / `Containers` | Switch tab |

## Command Line Options

```bash
g-systemctl --help              # Show help
g-systemctl --version           # Show version
g-systemctl --system            # Show system services (default)
g-systemctl --user              # Show user services
g-systemctl -f <text>           # Start with an initial filter
g-systemctl --filter <text>     # Start with an initial filter
g-systemctl                     # Containers tab, or Services if Docker is unavailable
g-systemctl --services          # Open the Services tab (-s)
g-systemctl --containers        # Open the Containers tab even without Docker (-c)
```

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](docs/CONTRIBUTING.md) for details on how to contribute to this project.

## License

g-systemctl is released under the [MIT License](LICENSE).
