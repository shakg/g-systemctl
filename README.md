# G-Systemctl

![Test Status](https://github.com/shakg/g-systemctl/actions/workflows/test.yml/badge.svg)
![Build Status](https://github.com/shakg/g-systemctl/actions/workflows/release.yml/badge.svg)

g-systemctl is a terminal user interface (TUI) for managing system services on Linux and macOS. View, search, filter, and control your service daemons directly from the terminal.

## See It In Action

https://github.com/user-attachments/assets/1608dc3a-20aa-46fd-b3ea-db577f4c197c



## Features

- List all system services with status indicators
- Filter services by name in real-time
- Start/stop services with a single keypress
- Cross-platform support (Linux via systemctl, macOS via launchctl)
- Keyboard-driven navigation
- Mouse support (scroll to navigate, click to select)
- Initial filter via command line argument (`-f` / `--filter`)
- Stream service logs in the TUI via `journalctl`

## Prerequisites

- CMake 3.14+
- C++17 compatible compiler (GCC 7+, Clang 5+)
- Linux with systemd or macOS
- `journalctl` for Linux service log streaming

## Installation

### Homebrew

Download the formula from the latest release and install it locally:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl.rb
brew install ./g-systemctl.rb
```

### Debian / Ubuntu

Download the `.deb` package from the latest release and install it:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-amd64.deb
sudo apt install ./g-systemctl-linux-amd64.deb
```

### Linux Binary

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-x86_64.tar.gz
tar -xzf g-systemctl-linux-x86_64.tar.gz
sudo install -m 755 g-systemctl-*/g-systemctl /usr/local/bin/g-systemctl
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

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/SHA256SUMS.txt
grep g-systemctl-linux-x86_64.tar.gz SHA256SUMS.txt | shasum -a 256 -c -
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
| `Up` / `Alt+k` | Move selection up |
| `Down` / `Alt+j` | Move selection down |
| `Enter` | Toggle selected service (start/stop) |
| `Alt+r` | Restart selected service |
| `Alt+l` | Stream logs for selected service |
| `Alt+c` | Close log stream |
| `Page Up` / `Page Down` | Scroll the log stream by page |
| `?` | Show/hide help |
| `Alt+q` / `Esc` | Quit |
| Type | Filter services by name |
| `Backspace` | Delete filter character |

### Mouse Support

| Action | Effect |
|--------|--------|
| Scroll up/down | Navigate the service list, or scroll the log panel when the pointer is over it |
| Left click service | Select a service |
| Left click action buttons | Toggle a service or open its logs |
| Left click `Shortcuts(?)` | Show help |

## Command Line Options

```bash
g-systemctl --help              # Show help
g-systemctl --version           # Show version
g-systemctl --system            # Show system services (default)
g-systemctl --user              # Show user services
g-systemctl -f <text>           # Start with an initial filter
g-systemctl --filter <text>     # Start with an initial filter
```

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](docs/CONTRIBUTING.md) for details on how to contribute to this project.
