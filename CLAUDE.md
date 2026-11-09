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
- `GraphToggle` (`Alt+g` or the log-header button, shared by both tabs) - At 100+ columns graphs sit beside the summary and are on by default; below that they replace the summary and are off by default. `GraphPanel` drops to one line per metric when there are fewer than two rows per metric
- `format_command` - Splits long commands (service ExecStart, docker entrypoint+cmd) into one option per line; the script of `sh -c`/`bash -c` is indented one level deeper
- `ConfirmDialog` - Owned by `App`, handed to `ContainersView` via `set_confirm`. Stop and restart go through it (or through the sudo password prompt, which replaces it). Only `y` or a click confirms; Enter is swallowed
- `ContainersView` - Containers tab. A poller thread refreshes `docker ps`/`docker stats` every 2s while the tab is visible; actions run on a worker thread and post results back with `ScreenInteractive::Post`. The detail page summary comes from `docker inspect` plus the live `docker stats` sample. `docker logs -f` exits when a container stops, so the log strip reattaches with `--since` once the container (or a recreated one with the same name) is running again

**Website (`landing-page/`):**

Astro static site: landing page + Markdown blog, deployed to GitHub Pages by `.github/workflows/pages.yml`.

```bash
cd landing-page
npm ci
npm run dev      # http://localhost:4321/
npm run build    # astro check + static build into dist/
```

- `src/pages/index.astro` - landing page markup; `src/scripts/landing.js` drives its animations; `src/styles/landing.css` its styles
- `src/content/blog/*.md` - blog posts (schema in `src/content.config.ts`: title ≤70 chars, description 50–160 chars, date, tags, draft)
- `src/components/BaseHead.astro` - SEO tags (canonical, Open Graph, Twitter, JSON-LD), self-hosted font preloads
- `src/lib/github.ts` - star count and latest release tag, fetched at build time (no client API calls)
- Site URL and base path come from `SITE_URL` / `SITE_BASE` (default `https://g-systemctl.org` + `/`). Link with `url()` from `src/lib/site.ts`, never hard-coded `/` paths
- Look: soft dark base with pastel accents (tokens in `src/styles/global.css`). Surfaces are filled panels with no border, shadow or glow; 1px lines are used only for the page column frame (`.frame`), section strips (`SectionBar`) and grid cell dividers
- Keep Lighthouse at 100: CSS is inlined, below-the-fold scripts start via `idle()`, and the hero terminal reserves its height to avoid layout shift

**FTXUI Components Used:**
- `ScreenInteractive::Fullscreen()` for terminal control
- `Renderer` for custom rendering
- `CatchEvent` for keyboard handling
- `vbox`, `hbox`, `border`, `separator` for layout
- `color`, `bold`, `dim`, `inverted` for styling
