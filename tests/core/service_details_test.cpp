#include <gtest/gtest.h>
#include <g-systemctl/platform/linux_service_manager.hpp>
#include <g-systemctl/platform/macos_service_manager.hpp>
#include <string>

namespace gsystemctl {

namespace {

std::string find(const ServiceDetails& rows, const std::string& label) {
    for (const auto& [key, value] : rows) {
        if (key == label) {
            return value;
        }
    }
    return "<missing>";
}

} // namespace

TEST(ServiceDetailsTest, ParsesSystemctlShowOutput) {
    const std::string output =
        "Type=forking\n"
        "Restart=on-failure\n"
        "NRestarts=2\n"
        "MainPID=812\n"
        "ExecStart={ path=/usr/sbin/nginx ; argv[]=/usr/sbin/nginx -g daemon on; ; ignore_errors=no ; start_time=[n/a] ; pid=0 }\n"
        "MemoryCurrent=10485760\n"
        "CPUUsageNSec=75500000000\n"
        "TasksCurrent=3\n"
        "User=\n"
        "Description=A high performance web server\n"
        "LoadState=loaded\n"
        "ActiveState=active\n"
        "SubState=running\n"
        "FragmentPath=/lib/systemd/system/nginx.service\n"
        "UnitFileState=enabled\n"
        "WantedBy=multi-user.target\n"
        "ActiveEnterTimestamp=Tue 2026-11-03 10:00:00 UTC\n"
        "InactiveEnterTimestamp=\n";

    auto rows = LinuxServiceManager::parse_show_output(output);

    EXPECT_EQ(rows.front().first, "Description");
    EXPECT_EQ(find(rows, "State"), "active (running) since Tue 2026-11-03 10:00:00 UTC");
    EXPECT_EQ(find(rows, "Unit file"), "enabled · /lib/systemd/system/nginx.service");
    EXPECT_EQ(find(rows, "Main PID"), "812");
    EXPECT_EQ(find(rows, "Memory"), "10.0 MiB");
    EXPECT_EQ(find(rows, "CPU time"), "1min 15.5s");
    EXPECT_EQ(find(rows, "Restart"), "on-failure · 2 restarts");
    EXPECT_EQ(find(rows, "Command"), "/usr/sbin/nginx -g daemon on;");
    EXPECT_EQ(find(rows, "User"), "<missing>");
}

TEST(ServiceDetailsTest, SkipsUnsetCountersOfStoppedService) {
    const std::string output =
        "ActiveState=inactive\n"
        "SubState=dead\n"
        "MainPID=0\n"
        "MemoryCurrent=[not set]\n"
        "CPUUsageNSec=18446744073709551615\n"
        "TasksCurrent=[not set]\n"
        "InactiveEnterTimestamp=Wed 2026-11-04 08:00:00 UTC\n"
        "LoadState=loaded\n";

    auto rows = LinuxServiceManager::parse_show_output(output);

    EXPECT_EQ(find(rows, "State"), "inactive (dead) since Wed 2026-11-04 08:00:00 UTC");
    EXPECT_EQ(find(rows, "Unit file"), "loaded");
    EXPECT_EQ(find(rows, "Main PID"), "<missing>");
    EXPECT_EQ(find(rows, "Memory"), "<missing>");
    EXPECT_EQ(find(rows, "CPU time"), "<missing>");
    EXPECT_EQ(find(rows, "Tasks"), "<missing>");
}

TEST(ServiceDetailsTest, ParsesLaunchctlListDictionary) {
    const std::string output =
        "{\n"
        "\t\"LimitLoadToSessionType\" = \"Aqua\";\n"
        "\t\"Label\" = \"com.example.agent\";\n"
        "\t\"PID\" = 412;\n"
        "\t\"ProgramArguments\" = (\n"
        "\t\t\"/usr/local/bin/agent\";\n"
        "\t\t\"--verbose\";\n"
        "\t);\n"
        "};\n";

    auto rows = MacOSServiceManager::parse_launchctl_details(output);

    EXPECT_EQ(find(rows, "Label"), "com.example.agent");
    EXPECT_EQ(find(rows, "PID"), "412");
    EXPECT_EQ(find(rows, "ProgramArguments"), "/usr/local/bin/agent --verbose");
}

TEST(ServiceDetailsTest, ParsesSampleOutput) {
    auto sample = LinuxServiceManager::parse_sample_output(
        "CPUUsageNSec=3506000000\n"
        "MemoryCurrent=569376768\n"
        "TasksCurrent=72\n"
        "IOReadBytes=18446744073709551615\n"
        "IOWriteBytes=[not set]\n");
    EXPECT_DOUBLE_EQ(sample.cpu_nsec, 3506000000.0);
    EXPECT_DOUBLE_EQ(sample.memory_bytes, 569376768.0);
    EXPECT_DOUBLE_EQ(sample.tasks, 72.0);
    EXPECT_LT(sample.io_read_bytes, 0);
    EXPECT_LT(sample.io_write_bytes, 0);
    EXPECT_FALSE(sample.empty());
    EXPECT_TRUE(LinuxServiceManager::parse_sample_output("MemoryCurrent=[not set]\n").empty());
}

} // namespace gsystemctl
