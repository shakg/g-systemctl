#include "g-systemctl/ui/app.hpp"
#include "g-systemctl/platform/platform.hpp"
#include <iostream>
#include <cstring>

void print_help() {
    std::cout << "g-systemctl - Terminal UI for system service management\n\n";
    std::cout << "Usage: g-systemctl [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help     Show this help message\n";
    std::cout << "  -v, --version  Show version information\n\n";
    std::cout << "Note: Run with sudo for full functionality (start/stop services)\n";
}

void print_version() {
    std::cout << "g-systemctl version 1.0.0\n";
    std::cout << "Platform: " << gsystemctl::platform_name(gsystemctl::detect_platform()) << "\n";
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
    }

    try {
        gsystemctl::ui::App app;
        return app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
