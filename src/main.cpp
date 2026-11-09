#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <iostream>
#include <cstring>

void print_help()
{
    std::cout << "g-systemctl - Terminal UI for system service management\n\n";
    std::cout << "Usage: g-systemctl [options]\n\n";
    std::cout << "Opens the Containers tab, or Services if Docker is unavailable.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message\n";
    std::cout << "  -v, --version           Show version information\n";
    std::cout << "  --system                Show system services (default)\n";
    std::cout << "  --user                  Show user services\n";
    std::cout << "  -c, --containers        Open the Containers tab, even without Docker\n";
    std::cout << "  -s, --services          Open the Services tab\n";
    std::cout << "  -f, --filter <text>     Set the initial filter text\n\n";
    std::cout << "Environment:\n";
    std::cout << "  GSYSTEMCTL_DOCKER       Docker CLI binary (default: docker)\n";
    std::cout << "  GSYSTEMCTL_COMPOSE      Compose command (default: docker compose)\n";
    std::cout << "  GSYSTEMCTL_EDITOR       Editor for unit files (default: vim)\n\n";
    std::cout << "Note: Run with sudo for full functionality (start/stop services)\n";
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
    // Without an explicit tab, fall back to Services when Docker is unavailable.
    bool fallback_to_services = true;

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
        gsystemctl::ui::App app(system_mode, initial_filter, initial_tab, fallback_to_services);
        return app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
