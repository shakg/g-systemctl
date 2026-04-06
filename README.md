# G-Systemctl

![Test Status](https://github.com/shakg/g-systemctl/actions/workflows/test.yml/badge.svg)
![Build Status](https://github.com/shakg/g-systemctl/actions/workflows/release.yml/badge.svg)

g-systemctl is a terminal user interface (TUI) for managing system services on Linux and macOS. View, search, filter, and control your service daemons directly from the terminal.

## See It In Action

[Screencast from 2026-01-09 00-45-46.webm](https://github.com/user-attachments/assets/647f492d-4076-4f2f-9484-c8c2f360656a)


## Features

- List all system services with status indicators
- Filter services by name in real-time
- Start/stop services with a single keypress
- Cross-platform support (Linux via systemctl, macOS via launchctl)
- Keyboard-driven navigation
- Mouse support (scroll to navigate, click to select)
- Initial filter via command line argument (`-f` / `--filter`)
- View service logs via journalctl (requires tmux)

## Prerequisites

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+)
- Linux with systemd or macOS
- tmux (optional, required for viewing service logs)

## Installation

### Build from source

```bash
git clone https://github.com/shakg/g-systemctl.git
cd g-systemctl
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

### One-Line Release Download

```bash
curl https://api.github.com/repos/shakg/g-systemctl/releases/latest | jq '.assets[0].browser_download_url' | xargs wget
```

## Usage

```bash
g-systemctl
```

For full functionality (start/stop services), run with sudo:

```bash
sudo g-systemctl
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Up` / `k` | Move selection up |
| `Down` / `j` | Move selection down |
| `Enter` | Toggle selected service (start/stop) |
| `r` | Refresh service list |
| `l` | Open logs for selected service (tmux only) |
| `?` | Show/hide help |
| `q` / `Esc` | Quit |
| Type | Filter services by name |
| `Backspace` | Delete filter character |

### Mouse Support

| Action | Effect |
|--------|--------|
| Scroll up/down | Navigate the service list |
| Left click | Select a service |

## Command Line Options

```bash
g-systemctl --help              # Show help
g-systemctl --version           # Show version
g-systemctl --system            # Show system services instead of user services
g-systemctl -f <text>           # Start with an initial filter
g-systemctl --filter <text>     # Start with an initial filter
```

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](docs/CONTRIBUTING.md) for details on how to contribute to this project.
