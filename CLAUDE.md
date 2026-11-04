# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

g-systemctl is a C++17 terminal user interface (TUI) application for managing system services on Linux (systemctl) and macOS (launchctl). Built with the FTXUI library.

## Build Commands

```bash
# Configure and build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j$(nproc)

# Run
./g-systemctl

# Install
sudo make install
```

## Architecture

**Project Structure:**
```
g-systemctl/
├── CMakeLists.txt              # Build configuration, fetches FTXUI
├── cmake/FetchFTXUI.cmake      # FTXUI dependency management
├── include/g-systemctl/
│   ├── core/                   # Core abstractions
│   │   ├── service.hpp         # ServiceUnit data structure
│   │   ├── service_manager.hpp # Abstract service manager interface
│   │   └── command_executor.hpp # Shell command execution
│   ├── container/              # Container runtime support
│   │   ├── container.hpp       # Container, ContainerDetails, resource-limit diffing
│   │   ├── container_manager.hpp # Abstract container runtime interface
│   │   └── docker_container_manager.hpp # docker CLI implementation
│   ├── platform/               # Platform-specific implementations
│   │   ├── platform.hpp        # Platform detection
│   │   ├── linux_service_manager.hpp
│   │   └── macos_service_manager.hpp
│   └── ui/
│       ├── app.hpp             # Main application class, tabs
│       ├── containers_view.hpp # Containers tab
│       ├── log_panel.hpp       # Scrollable log panel shared by both tabs
│       ├── theme.hpp           # Palette and table-cell helpers
│       └── styles.hpp          # UI styling/colors
└── src/                        # Implementation files
```

**Key Components:**

- `ServiceManager` - Abstract interface for service operations. Factory method creates platform-appropriate implementation.
- `LinuxServiceManager` - Uses `systemctl list-units` and `systemctl start/stop`
- `MacOSServiceManager` - Uses `launchctl list` and `launchctl load/unload`
- `CommandExecutor` - Wraps shell command execution, handles sudo for privileged operations
- `ContainerManager` - Abstract container runtime interface. `DockerContainerManager` drives the `docker` CLI with tab-separated Go-template `--format` output (no JSON dependency)
- `ProcessLogStream` - Runs a command and streams its output lines from a background thread (journalctl, docker logs)
- `App` - FTXUI-based TUI with Services and Containers tabs, filtering, and keyboard navigation
- `ContainersView` - Containers tab. A poller thread refreshes `docker ps`/`docker stats` every 2s while the tab is visible; actions run on a worker thread and post results back with `ScreenInteractive::Post`. `docker logs -f` exits when a container stops, so the log panel reattaches with `--since` once the container (or a recreated one with the same name) is running again

**FTXUI Components Used:**
- `ScreenInteractive::Fullscreen()` for terminal control
- `Renderer` for custom rendering
- `CatchEvent` for keyboard handling
- `vbox`, `hbox`, `border`, `separator` for layout
- `color`, `bold`, `dim`, `inverted` for styling
