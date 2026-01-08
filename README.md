# G-Systemctl

![Build Status](https://github.com/shakg/g-systemctl/actions/workflows/test.yml/badge.svg)

g-systemctl is a terminal user interface (TUI) for managing system services on Linux and macOS. View, search, filter, and control your service daemons directly from the terminal.

## Features

- List all system services with status indicators
- Filter services by name in real-time
- Start/stop services with a single keypress
- Cross-platform support (Linux via systemctl, macOS via launchctl)
- Keyboard-driven navigation

## Prerequisites

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+)
- Linux with systemd or macOS

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
| `?` | Show/hide help |
| `q` / `Esc` | Quit |
| Type | Filter services by name |
| `Backspace` | Delete filter character |

## Command Line Options

```bash
g-systemctl --help     # Show help
g-systemctl --version  # Show version
```

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](docs/CONTRIBUTING.md) for details on how to contribute to this project.
