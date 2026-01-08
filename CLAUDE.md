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
│   ├── platform/               # Platform-specific implementations
│   │   ├── platform.hpp        # Platform detection
│   │   ├── linux_service_manager.hpp
│   │   └── macos_service_manager.hpp
│   └── ui/
│       ├── app.hpp             # Main application class
│       └── styles.hpp          # UI styling/colors
└── src/                        # Implementation files
```

**Key Components:**

- `ServiceManager` - Abstract interface for service operations. Factory method creates platform-appropriate implementation.
- `LinuxServiceManager` - Uses `systemctl list-units` and `systemctl start/stop`
- `MacOSServiceManager` - Uses `launchctl list` and `launchctl load/unload`
- `CommandExecutor` - Wraps shell command execution, handles sudo for privileged operations
- `App` - FTXUI-based TUI with service list, filtering, and keyboard navigation

**FTXUI Components Used:**
- `ScreenInteractive::Fullscreen()` for terminal control
- `Renderer` for custom rendering
- `CatchEvent` for keyboard handling
- `vbox`, `hbox`, `border`, `separator` for layout
- `color`, `bold`, `dim`, `inverted` for styling
