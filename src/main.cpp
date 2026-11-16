#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/core/command_executor.hpp"
#include "g-systemctl/core/updater.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <iostream>
#include <cstring>
#include <memory>
#include <unistd.h>

void print_help()
{
    std::cout << "g-systemctl - Terminal UI for system service management\n\n";
    std::cout << "Usage: g-systemctl [options]\n\n";
    std::cout << "Opens the Containers tab, or Services if neither docker nor podman is available.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  -v, --version           Show version information\n";
    std::cout << "  --system                Show system services (default)\n";
    std::cout << "  --user                  Show user services\n";
    std::cout << "  -c, --containers        Open the Containers tab, even without a runtime\n";
    std::cout << "  -s, --services          Open the Services tab\n";
    std::cout << "  -f, --filter <text>     Set the initial filter text\n";
    std::cout << "  --update                Install the latest release now\n";
    std::cout << "  --no-update-check       Do not check for a new release on this run\n\n";
    std::cout << "Environment:\n";
    std::cout << "  GSYSTEMCTL_RUNTIME      docker, podman or auto (default: auto, docker first)\n";
    std::cout << "  GSYSTEMCTL_DOCKER       Container CLI binary (default: docker or podman)\n";
    std::cout << "  GSYSTEMCTL_COMPOSE      Compose command (default: <runtime> compose)\n";
    std::cout << "  GSYSTEMCTL_EDITOR       Editor for unit files (default: vim)\n";
    std::cout << "  GSYSTEMCTL_NO_UPDATE    Set to 1 to never check for new releases\n\n";
    std::cout << "Release builds check GitHub for a new version once a day and ask before installing it.\n";
    std::cout << "Turn this off for good with check=off in ~/.config/g-systemctl/update.\n\n";
    std::cout << "Note: Run with sudo for full functionality (start/stop services)\n";
}

// --update: check now (ignoring the daily limit and the opt-out) and install
// on this terminal, so sudo can ask for a password.
int run_update()
{
    gsystemctl::Updater updater(std::make_shared<gsystemctl::SystemCommandExecutor>(), G_SYSTEMCTL_VERSION);
    if (!gsystemctl::parse_release_version(G_SYSTEMCTL_VERSION))
    {
        std::cerr << "This is a development build (" << G_SYSTEMCTL_VERSION
                  << "); install a release by hand (see the README).\n";
        return 1;
    }
    std::cout << "Checking for a new release..." << std::endl;
    std::string error;
    auto release = updater.check(&error);
    if (!release)
    {
        if (!error.empty())
        {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << "g-systemctl " << G_SYSTEMCTL_VERSION << " is up to date.\n";
        return 0;
    }
    auto plan = updater.plan(*release);
    if (!plan.error.empty())
    {
        std::cerr << "g-systemctl " << release->version << " is available. " << plan.error << "\n";
        return 1;
    }
    std::cout << "Installing g-systemctl " << release->version << " (you have " << G_SYSTEMCTL_VERSION << ") to "
              << plan.target << (plan.needs_root && geteuid() != 0 ? " with sudo" : "") << "..." << std::endl;
    auto [ok, message] = updater.install(*release, plan, true);
    if (!ok)
    {
        std::cerr << message << "\n";
        return 1;
    }
    std::cout << message << ".\n";
    return 0;
}

void print_version()
{
    std::cout << "g-systemctl version " << G_SYSTEMCTL_VERSION << "\n";
    std::cout << "Platform: " << gsystemctl::platform_name(gsystemctl::detect_platform()) << "\n";
}

int main(int argc, char *argv[])
{
    bool system_mode = true;
    std::string initial_filter;
    auto initial_tab = gsystemctl::ui::App::Tab::Containers;
    // Without an explicit tab, fall back to Services when no container runtime is available.
    bool fallback_to_services = true;
    bool update_check = true;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0)
        {
            print_help();
            return 0;
        }
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--version") == 0)
        {
            print_version();
            return 0;
        }
        if (std::strcmp(argv[i], "--update") == 0)
        {
            return run_update();
        }
        if (std::strcmp(argv[i], "--no-update-check") == 0)
        {
            update_check = false;
        }
        if (std::strcmp(argv[i], "--system") == 0)
        {
            system_mode = true;
        }
        if (std::strcmp(argv[i], "--user") == 0)
        {
            system_mode = false;
        }
        if (std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--containers") == 0)
        {
            initial_tab = gsystemctl::ui::App::Tab::Containers;
            fallback_to_services = false;
        }
        if (std::strcmp(argv[i], "-s") == 0 || std::strcmp(argv[i], "--services") == 0)
        {
            initial_tab = gsystemctl::ui::App::Tab::Services;
            fallback_to_services = false;
        }
        if ((std::strcmp(argv[i], "-f") == 0 || std::strcmp(argv[i], "--filter") == 0) && i + 1 < argc)
        {
            initial_filter = argv[++i];
        }
    }

    try
    {
        gsystemctl::ui::App app(system_mode, initial_filter, initial_tab, fallback_to_services, update_check);
        return app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
