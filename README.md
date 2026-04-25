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

### Homebrew

Download the formula from the latest release and install it locally:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl.rb
brew install ./g-systemctl.rb
```

### Debian / Ubuntu

Install from the APT repository:

```bash
curl -sSL https://shakg.github.io/g-systemctl/repo.gpg \
    | sudo gpg --dearmor \
    -o /usr/share/keyrings/g-systemctl.gpg

echo "deb [signed-by=/usr/share/keyrings/g-systemctl.gpg] https://shakg.github.io/g-systemctl stable main" \
    | sudo tee /etc/apt/sources.list.d/g-systemctl.list

sudo apt update
sudo apt install g-systemctl
```

Or download the `.deb` package from the latest release and install it directly:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl_1.0.0_amd64.deb
sudo apt install ./g-systemctl_1.0.0_amd64.deb
```

Replace `1.0.0` with the version from the release you are installing.

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
| `Up` / `k` | Move selection up |
| `Down` / `j` | Move selection down |
| `Enter` | Toggle selected service (start/stop) |
| `Alt+r` | Restart selected service |
| `Alt+l` | Open logs for selected service (tmux only) |
| `?` | Show/hide help |
| `Alt+q` / `Esc` | Quit |
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
g-systemctl --system            # Show system services (default)
g-systemctl --user              # Show user services
g-systemctl -f <text>           # Start with an initial filter
g-systemctl --filter <text>     # Start with an initial filter
```

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](docs/CONTRIBUTING.md) for details on how to contribute to this project.

## Release Maintainer Setup

The release workflow publishes the APT repository to the `gh-pages` branch on tagged releases.

One-time setup:

```bash
cat > g-systemctl-apt-key.conf <<'EOF'
Key-Type: RSA
Key-Length: 4096
Name-Real: g-systemctl APT Repository
Name-Email: noreply@github.com
Expire-Date: 2y
%no-protection
%commit
EOF

gpg --batch --generate-key g-systemctl-apt-key.conf
gpg --armor --export-secret-keys "g-systemctl APT Repository <noreply@github.com>"
```

Add the exported private key as the `APT_GPG_PRIVATE_KEY` repository secret. If you create a passphrase-protected key instead, add the passphrase as `APT_GPG_PASSPHRASE`.

In GitHub repository settings, enable Pages from the `gh-pages` branch.
