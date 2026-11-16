#include <gtest/gtest.h>
#include <g-systemctl/core/updater.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace gsystemctl {

TEST(UpdaterTest, ParsesReleaseVersions) {
    auto v = parse_release_version("v2.3.0");
    ASSERT_TRUE(v);
    EXPECT_EQ((*v)[0], 2);
    EXPECT_EQ((*v)[1], 3);
    EXPECT_EQ((*v)[2], 0);
    EXPECT_TRUE(parse_release_version("10.0.12"));
    // Dev builds never update themselves.
    EXPECT_FALSE(parse_release_version("0.0.0-dev"));
    EXPECT_FALSE(parse_release_version("2.0.1-3-gabc1234"));
    EXPECT_FALSE(parse_release_version("0.0.0-abc1234"));
    EXPECT_FALSE(parse_release_version("2.3"));
    EXPECT_FALSE(parse_release_version(""));
    EXPECT_FALSE(parse_release_version("v"));
}

TEST(UpdaterTest, ComparesVersionsNumerically) {
    EXPECT_TRUE(is_newer_release("v2.3.0", "2.2.9"));
    EXPECT_TRUE(is_newer_release("v2.10.0", "2.9.0"));
    EXPECT_TRUE(is_newer_release("3.0.0", "2.99.99"));
    EXPECT_FALSE(is_newer_release("v2.3.0", "2.3.0"));
    EXPECT_FALSE(is_newer_release("v2.2.0", "2.3.0"));
    EXPECT_FALSE(is_newer_release("v2.3.0", "0.0.0-dev"));
}

TEST(UpdaterTest, ParsesLatestTagFromGithubJson) {
    const std::string json = R"({
  "url": "https://api.github.com/repos/shakg/g-systemctl/releases/1",
  "tag_name": "v2.3.0",
  "name": "v2.3.0"
})";
    EXPECT_EQ(Updater::parse_latest_tag(json), "v2.3.0");
    EXPECT_EQ(Updater::parse_latest_tag(R"({"tag_name":"v1.0.0"})"), "v1.0.0");
    EXPECT_EQ(Updater::parse_latest_tag(R"({"message":"API rate limit exceeded"})"), "");
}

TEST(UpdaterTest, FindsChecksum) {
    const std::string a(64, 'a');
    const std::string b(64, 'b');
    const std::string sums = a + "  g-systemctl-linux-x86_64.tar.gz\n" + b + " *g-systemctl-linux-amd64.deb\n";
    EXPECT_EQ(Updater::find_checksum(sums, "g-systemctl-linux-x86_64.tar.gz"), a);
    EXPECT_EQ(Updater::find_checksum(sums, "g-systemctl-linux-amd64.deb"), b);
    EXPECT_EQ(Updater::find_checksum(sums, "g-systemctl-macos.tar.gz"), "");
    EXPECT_EQ(Updater::find_checksum("short  g-systemctl-macos.tar.gz\n", "g-systemctl-macos.tar.gz"), "");
}

TEST(UpdaterTest, StateRoundTrips) {
    Updater::State state;
    state.check = false;
    state.last_check = 1700000000;
    state.latest = "v2.3.0";
    state.skip = "v2.2.0";
    auto parsed = Updater::parse_state(Updater::serialize_state(state));
    EXPECT_FALSE(parsed.check);
    EXPECT_EQ(parsed.last_check, 1700000000);
    EXPECT_EQ(parsed.latest, "v2.3.0");
    EXPECT_EQ(parsed.skip, "v2.2.0");

    EXPECT_TRUE(Updater::parse_state("").check);
    EXPECT_FALSE(Updater::parse_state("check = no\n").check);
}

TEST(UpdaterTest, PlansBinaryInstalls) {
    auto p = Updater::make_plan("linux", "x86_64", "/usr/local/bin/g-systemctl", false, false);
    EXPECT_TRUE(p.error.empty());
    EXPECT_EQ(p.kind, Updater::InstallKind::Binary);
    EXPECT_EQ(p.asset, "g-systemctl-linux-x86_64.tar.gz");
    EXPECT_EQ(p.sums_asset, "SHA256SUMS-linux-x86_64.txt");
    EXPECT_TRUE(p.needs_root);

    p = Updater::make_plan("linux", "aarch64", "/home/me/.local/bin/g-systemctl", false, true);
    EXPECT_EQ(p.asset, "g-systemctl-linux-aarch64.tar.gz");
    EXPECT_FALSE(p.needs_root);

    p = Updater::make_plan("macos", "arm64", "/usr/local/bin/g-systemctl", false, true);
    EXPECT_EQ(p.asset, "g-systemctl-macos.tar.gz");
    EXPECT_EQ(p.sums_asset, "SHA256SUMS-macos.txt");
}

TEST(UpdaterTest, PlansDebInstalls) {
    auto p = Updater::make_plan("linux", "aarch64", "/usr/bin/g-systemctl", true, false);
    EXPECT_TRUE(p.error.empty());
    EXPECT_EQ(p.kind, Updater::InstallKind::Deb);
    EXPECT_EQ(p.asset, "g-systemctl-linux-arm64.deb");
    EXPECT_EQ(p.sums_asset, "SHA256SUMS-linux-aarch64.txt");
    EXPECT_TRUE(p.needs_root);
}

TEST(UpdaterTest, RefusesWhatItCannotUpdate) {
    auto p = Updater::make_plan("macos", "arm64", "/opt/homebrew/Cellar/g-systemctl/2.2.0/bin/g-systemctl", false, true);
    EXPECT_EQ(p.kind, Updater::InstallKind::Homebrew);
    EXPECT_FALSE(p.error.empty());
    EXPECT_FALSE(Updater::make_plan("macos", "x86_64", "/usr/local/bin/g-systemctl", false, true).error.empty());
    EXPECT_FALSE(Updater::make_plan("linux", "riscv64", "/usr/local/bin/g-systemctl", false, true).error.empty());
    EXPECT_FALSE(Updater::make_plan("linux", "x86_64", "", false, true).error.empty());
}

TEST(UpdaterTest, CachedUpdateHonoursSkip) {
    char dir[] = "/tmp/g-systemctl-updater-test.XXXXXX";
    ASSERT_NE(mkdtemp(dir), nullptr);
    const std::string path = std::string(dir) + "/update";
    {
        std::ofstream(path) << "latest=v2.3.0\n";
    }
    {
        Updater updater(nullptr, "2.2.0", path);
        auto update = updater.cached_update();
        ASSERT_TRUE(update);
        EXPECT_EQ(update->tag, "v2.3.0");
        EXPECT_EQ(update->version, "2.3.0");
        updater.skip("v2.3.0");
        EXPECT_FALSE(updater.cached_update());
        updater.disable_checks();
    }
    Updater reloaded(nullptr, "2.2.0", path);
    EXPECT_FALSE(reloaded.cached_update());
    EXPECT_FALSE(reloaded.checks_enabled());
    EXPECT_FALSE(Updater(nullptr, "2.3.0", path).cached_update()); // already on it
    std::remove(path.c_str());
    rmdir(dir);
}

TEST(UpdaterTest, DevBuildsDoNotCheck) {
    Updater dev(nullptr, "0.0.0-dev", "");
    std::string reason;
    EXPECT_FALSE(dev.checks_enabled(&reason));
    EXPECT_NE(reason.find("development"), std::string::npos);
    EXPECT_TRUE(Updater(nullptr, "2.2.0", "").checks_enabled());
}

TEST(UpdaterTest, DownloadUrl) {
    EXPECT_EQ(Updater::download_url("v2.3.0", "g-systemctl-macos.tar.gz"),
              "https://github.com/shakg/g-systemctl/releases/download/v2.3.0/g-systemctl-macos.tar.gz");
}

} // namespace gsystemctl
