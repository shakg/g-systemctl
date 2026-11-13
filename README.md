# g-systemctl

![Test Status](https://github.com/shakg/g-systemctl/actions/workflows/test.yml/badge.svg)
![Build Status](https://github.com/shakg/g-systemctl/actions/workflows/release.yml/badge.svg)
![Podman](https://github.com/shakg/g-systemctl/actions/workflows/podman.yml/badge.svg)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A terminal UI for Docker or Podman containers and system services (systemctl on Linux, launchctl on macOS).

![g-systemctl containers tab](docs/screenshot.png)

## Features

- Containers: live CPU/memory/PIDs, start/stop/restart, logs, inspect, change resource limits in place
- Compose: containers grouped by project; start/stop/restart, `up -d`, build, pull, down; flags changed compose files
- Docker or Podman, picked automatically (see [Podman](#podman))
- Services: list, filter, start/stop/restart, follow `journalctl` logs, edit unit files
- Detail page with summary, graphs and live logs
- Keyboard and mouse

## Install

Debian / Ubuntu (22.04+ / 12+), use `arm64` instead of `amd64` for ARM:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-amd64.deb
sudo apt install ./g-systemctl-linux-amd64.deb
```

Other Linux (glibc 2.34+):

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl-linux-$(uname -m).tar.gz
tar -xzf g-systemctl-linux-$(uname -m).tar.gz
sudo install -m 755 g-systemctl-*/g-systemctl /usr/local/bin/g-systemctl
```

macOS:

```bash
curl -LO https://github.com/shakg/g-systemctl/releases/latest/download/g-systemctl.rb
brew install ./g-systemctl.rb
```

From source (CMake 3.14+, C++17 compiler):

```bash
git clone https://github.com/shakg/g-systemctl.git
cd g-systemctl && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
sudo make install
```

## Usage

```bash
g-systemctl              # Containers tab, or Services if no container runtime is available
g-systemctl -s           # Services tab
g-systemctl -c           # Containers tab
g-systemctl --user       # User services
g-systemctl -f nginx     # Start with a filter
```

Starting and stopping services needs root; run with `sudo` or enter the password when asked.

| Key | Action |
|-----|--------|
| Type | Filter |
| `Up` / `Down` | Move |
| `Enter` / `Esc` | Open / back |
| `Alt+o` / `Alt+x` / `Alt+r` | Start / stop / restart (stop and restart ask y/n) |
| `Alt+1` / `Alt+2` | Containers / Services tab |
| `?` | All shortcuts |
| `Alt+q` | Quit |

## Podman

g-systemctl uses `docker` when its daemon is reachable, otherwise `podman`. A `docker` command that is really Podman (podman-docker) is treated as Podman. To choose:

```bash
GSYSTEMCTL_RUNTIME=podman g-systemctl    # docker | podman | auto (default)
GSYSTEMCTL_DOCKER=/opt/bin/podman g-systemctl   # CLI binary
GSYSTEMCTL_COMPOSE=podman-compose g-systemctl   # compose command
```

- Compose uses `podman compose`, then `podman-compose`. With `podman-compose`, changed compose files are not flagged: it cannot print config hashes. `podman compose` with the docker-compose provider can.
- Pods' infra containers are hidden.
- Rootless and `sudo` Podman keep separate containers. Run g-systemctl the same way you run `podman`.
- macOS: start the VM first with `podman machine start`.
- Podman 4.x: `podman update` applies new limits, but `inspect` keeps showing the old ones, and it has no `--restart`.

Tested in CI against Podman 4.9 (Ubuntu 24.04), rootless and rootful, with both compose commands.

## Contributing

See [CONTRIBUTING.md](docs/CONTRIBUTING.md).

## License

[MIT](LICENSE)
