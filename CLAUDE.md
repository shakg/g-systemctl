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
│   │   ├── command_format.hpp  # Lays out long commands one option per line
│   │   └── text_wrap.hpp       # Splits a line into rows of a given column width
│   ├── container/              # Container runtime support
│   │   ├── container.hpp       # Container, ContainerDetails, resource-limit diffing, compose grouping/drift
│   │   ├── compose_store.hpp   # Saved compose projects (~/.config/g-systemctl/compose-projects)
│   │   ├── container_manager.hpp # Abstract container runtime interface
│   │   └── cli_container_manager.hpp # docker/podman CLI implementation
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
- `ContainerManager` - Abstract container runtime interface. `CliContainerManager` drives the `docker` or `podman` CLI with tab-separated Go-template `--format` output (no JSON dependency)
- Podman - `CliContainerManager::select_runtime` picks the CLI: `GSYSTEMCTL_RUNTIME=docker|podman|auto`, `GSYSTEMCTL_DOCKER` overrides the binary. Auto prefers docker, uses podman when docker is missing or its daemon is unreachable, and treats a `docker` whose `--version` says podman (podman-docker) as podman. Podman differences live in `CliContainerManager`: `info` for the availability check, `.PIDS` in stats, `.HostIP` in inspect, pod infra containers hidden via `{{.IsInfra}}`, and podman 4's `{{index .Labels ...}}` (switched to when `.Label` errors). Compose on podman is `podman compose` then `podman-compose` (never standalone docker-compose); its stderr banner is stripped from output; podman-compose prints no `name:` in `config` (the directory name is used). Project logs on podman do not use compose at all: `podman logs -f -n` over the project's container IDs (podman-compose's `logs -f` forwarded nothing with the journald log driver) Drift needs `config --hash`, which podman-compose lacks (`compose_drift_supported`). `ProcessLogStream` runs its child in its own process group and kills the group, because `podman compose` does not forward SIGTERM to its provider. `tests/integration/` runs against a real runtime when `GSYSTEMCTL_INTEGRATION=1`; `.github/workflows/podman.yml` runs it via `tests/integration/podman-ci.sh` (rootless + rootful, `podman compose` + podman-compose) plus a tmux smoke test
- `ProcessLogStream` - Runs a command and streams its output lines from a background thread (journalctl, docker logs)
- `App` - FTXUI-based TUI with Containers (left, default, `Alt+1`) and Services (`Alt+2`) tabs, filtering, and keyboard navigation. Without `-s`/`-c` it falls back to Services when the first Docker check fails (`ContainersView::set_on_unavailable`). The help (`?`) has its own tabs: Containers, Services, General, Settings; it opens on the one matching the active tab and takes all input while open. Navigation is three levels: list → detail page (summary + short log strip) → full-size logs. `Enter` goes down, `Esc` goes up. Start/stop/restart are `Alt+o`/`Alt+x`/`Alt+r`; on the detail page they target the item shown
- `ServiceManager::describe_service` - Summary rows for the service detail page (`systemctl show -p ...` on Linux, `launchctl list <label>` on macOS); parsers are static and unit-tested
- `ServiceManager::sample_service` - Raw counters (CPU ns, memory, tasks, IO bytes) for the service detail graphs. `App` runs a sampler thread every 2s while a detail page is open; it also refreshes the summary rows. Empty on macOS, so no graphs there
- Container detail graphs come from the existing `docker stats` poll; `ContainersView` keeps a history per running container, so graphs are already filled when a detail page opens
- `GraphToggle` (`Alt+g` or the log-header button, shared by both tabs) - At 100+ columns graphs sit beside the summary and are on by default; below that they replace the summary and are off by default. `GraphPanel` drops to one line per metric when there are fewer than two rows per metric
- `LogPanel` wraps long log lines onto extra rows instead of clipping (`wrap_text`, hard break at the column limit). The width is only known after layout, so its node asks FTXUI for another layout pass (`Node::Check`). Scrolling still moves by log line
- `format_command` - Splits long commands (service ExecStart, docker entrypoint+cmd) into one option per line; the script of `sh -c`/`bash -c` is indented one level deeper
- `ConfirmDialog` - Owned by `App`, handed to `ContainersView` via `set_confirm`. Stop and restart go through it (or through the sudo password prompt, which replaces it). Only `y` or a click confirms; Enter is swallowed
- Compose - `ps` reads the compose labels (project, service, replica, working dir, config files, config hash, one-off, depends_on). `group_compose_projects` (`src/container/compose.cpp`, pure, unit-tested) groups them and adds saved projects that have no containers. `ContainerManager::compose` runs `cd DIR && docker compose -p NAME -f FILE... <action>`; start/stop/restart/down fall back to `-p NAME` alone if the files are gone. The poller compares each service's `config-hash` label with `compose config --hash '*'` (all projects every ~30s, the open project every poll) to flag drift. `ComposeStore` remembers projects so they stay listed after `down`
- `ContainersView` - Containers tab. List rows are project headers, their containers (tree lines) and standalone containers; when both groups are listed, each starts with a label row (`COMPOSE n projects`, `STANDALONE n containers`); selection is keyed by container ID or `project:NAME`. A project page reuses the detail layout with `compose logs -f` and summed graphs. On a project, `Alt+o/x/r` map to compose start/stop/restart (`up -d` when down), `Alt+u` to `up -d`, plus `Alt+b` build, `Alt+p` pull, `Alt+d` down, `Alt+a` add by path, `Alt+v` container → project. A poller thread refreshes `docker ps`/`docker stats` every 2s while the tab is visible; actions run on a worker thread and post results back with `ScreenInteractive::Post`. The detail page summary comes from `docker inspect` plus the live `docker stats` sample. `docker logs -f` exits when a container stops, so the log strip reattaches with `--since` once the container (or a recreated one with the same name) is running again

**Website (`landing-page/`):**

Astro static site: landing page + Markdown blog, served by a Cloudflare Worker with static assets (`landing-page/wrangler.jsonc`). `.github/workflows/deploy-site.yml` builds and deploys on pushes to `main` that touch `landing-page/` (needs `CLOUDFLARE_API_TOKEN` and `CLOUDFLARE_ACCOUNT_ID` secrets); `npm run deploy` deploys by hand.

```bash
cd landing-page
npm ci
npm run dev      # http://localhost:4321/
npm run build    # astro check + static build into dist/
npm run preview:worker  # build, then serve dist/ with wrangler dev like production
```

- `src/pages/index.astro` - landing page markup; `src/scripts/landing.js` drives its animations; `src/styles/landing.css` its styles
- `src/content/blog/*.md` - blog posts (schema in `src/content.config.ts`: title ≤70 chars, description 50–160 chars, date, tags, draft)
- `src/components/BaseHead.astro` - SEO tags (canonical, Open Graph, Twitter, JSON-LD), self-hosted font preloads
- Hero and Navigation demos replay frames recorded from the real binary (`src/data/demo.json`, rendered by `src/lib/term.js`). After a UI change, rebuild the app and re-record: `pip install pyte && python3 -I landing-page/tools/demo-capture/capture.py` (fake `systemctl`/`journalctl`/`ps`/`docker`/`sudo` live in `tools/demo-capture/fake.py`). Record at 120 columns; narrower layouts squeeze the containers list and the filter bar
- `src/lib/github.ts` - star count and latest release tag, fetched at build time (no client API calls)
- Analytics: self-hosted Umami. `BaseHead.astro` adds the tracker only when `PUBLIC_UMAMI_SCRIPT` and `PUBLIC_UMAMI_WEBSITE_ID` are set at build (CI reads repo variables `UMAMI_SCRIPT`, `UMAMI_WEBSITE_ID`); `data-domains` limits it to the production host. Custom events: `data-umami-event` on GitHub/contribute links, `umami.track("install_copy")` in `landing.js`
- Site URL and base path come from `SITE_URL` / `SITE_BASE` (default `https://g-systemctl.org` + `/`). Link with `url()` from `src/lib/site.ts`, never hard-coded `/` paths
- Look: soft dark base with pastel accents (tokens in `src/styles/global.css`). Surfaces are filled panels with no border, shadow or glow; 1px lines are used only for the page column frame (`.frame`) and one hairline between sections. Every section is heading + `.sub` line, then content inset inside the frame; grids (bento, keymap, FAQ) are filled cells with an 8px gap, not full-bleed hairline grids. No window chrome or traffic-light dots
- Feature-card mocks of the app must match the app exactly: square elements (the selected row is a flat band with an accent `▌` glyph, not a rounded box with a left border), real labels and prompts, and no behavior the app lacks (e.g. the filter does not highlight matches). Check a mock against a screenshot of the binary before changing it
- Keep Lighthouse at 100: CSS is inlined, below-the-fold scripts start via `idle()`, and the hero terminal reserves its height to avoid layout shift

**FTXUI Components Used:**
- `ScreenInteractive::Fullscreen()` for terminal control
- `Renderer` for custom rendering
- `CatchEvent` for keyboard handling
- `vbox`, `hbox`, `border`, `separator` for layout
- `color`, `bold`, `dim`, `inverted` for styling
