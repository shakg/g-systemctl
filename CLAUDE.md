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
│   │   ├── command_executor.hpp # Shell command execution
│   │   └── command_format.hpp  # Lays out long commands one option per line
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
│       ├── action_bar.hpp      # Start/Stop/Restart/Back and Expand/Shrink buttons on the detail page
│       ├── detail_page.hpp     # Detail page layout: title bar, scrollable summary (+ graphs on the right half), log strip
│       ├── metric_graph.hpp    # Sample history, counter rates and bar graphs for the detail pages
│       ├── confirm_dialog.hpp  # y/n prompt guarding stop and restart
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
- `App` - FTXUI-based TUI with Services and Containers tabs, filtering, and keyboard navigation. Navigation is three levels: list → detail page (summary + short log strip) → full-size logs. `Enter` goes down, `Esc` goes up. Start/stop/restart are `Alt+o`/`Alt+x`/`Alt+r`; on the detail page they target the item shown
- `ServiceManager::describe_service` - Summary rows for the service detail page (`systemctl show -p ...` on Linux, `launchctl list <label>` on macOS); parsers are static and unit-tested
- `ServiceManager::sample_service` - Raw counters (CPU ns, memory, tasks, IO bytes) for the service detail graphs. `App` runs a sampler thread every 2s while a detail page is open; it also refreshes the summary rows. Empty on macOS, so no graphs there
- Container detail graphs come from the existing `docker stats` poll; `ContainersView` keeps a history per running container, so graphs are already filled when a detail page opens
- `format_command` - Splits long commands (service ExecStart, docker entrypoint+cmd) into one option per line; the script of `sh -c`/`bash -c` is indented one level deeper
- `ConfirmDialog` - Owned by `App`, handed to `ContainersView` via `set_confirm`. Stop and restart go through it (or through the sudo password prompt, which replaces it). Only `y` or a click confirms; Enter is swallowed
- `ContainersView` - Containers tab. A poller thread refreshes `docker ps`/`docker stats` every 2s while the tab is visible; actions run on a worker thread and post results back with `ScreenInteractive::Post`. The detail page summary comes from `docker inspect` plus the live `docker stats` sample. `docker logs -f` exits when a container stops, so the log strip reattaches with `--since` once the container (or a recreated one with the same name) is running again

**FTXUI Components Used:**
- `ScreenInteractive::Fullscreen()` for terminal control
- `Renderer` for custom rendering
- `CatchEvent` for keyboard handling
- `vbox`, `hbox`, `border`, `separator` for layout
- `color`, `bold`, `dim`, `inverted` for styling
