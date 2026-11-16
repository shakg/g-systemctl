#pragma once

#include "g-systemctl/core/command_executor.hpp"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace gsystemctl {

// "2.3.0" or "v2.3.0" -> {2, 3, 0}. Nothing for dev builds ("0.0.0-dev",
// "2.0.1-3-gabc1234", "0.0.0-abc1234"), which never update themselves.
std::optional<std::array<int, 3>> parse_release_version(const std::string& version);
// False if either is not a release version.
bool is_newer_release(const std::string& latest, const std::string& current);

// Checks GitHub for a newer release and installs it over the running binary.
// Uses the curl (or wget), tar and sha256sum (or shasum) CLIs through the
// executor, like everything else here; no HTTP or JSON library.
//
// Install kinds:
//  - Binary: release tarball, verified against SHA256SUMS, test-run, then
//    renamed over the binary. Needs sudo if its directory is not writable.
//  - Deb: the binary belongs to the .deb; the new .deb goes through dpkg -i.
//  - Homebrew: not done here; the plan's error says how.
//
// State (last check, skipped version, opt-out) lives in a small key=value file.
// Check and install run on a background thread; the rest is quick.
class Updater {
public:
    using Result = std::pair<bool, std::string>;

    struct Release {
        std::string tag;     // "v2.3.0"
        std::string version; // "2.3.0"
    };

    enum class InstallKind { Binary, Deb, Homebrew };
    struct Plan {
        InstallKind kind = InstallKind::Binary;
        std::string target;     // the binary to replace
        std::string asset;      // tarball or .deb
        std::string sums_asset; // checksum file listing `asset`
        bool needs_root = false;
        // Non-empty: this install cannot update itself; says what to do instead.
        std::string error;
    };

    struct State {
        bool check = true;
        long long last_check = 0; // unix time
        std::string latest;       // tag seen at the last check
        std::string skip;         // tag the user chose to skip
    };

    static constexpr const char* kRepo = "shakg/g-systemctl";
    static constexpr long long kCheckInterval = 24 * 60 * 60;

    Updater(std::shared_ptr<CommandExecutor> executor, std::string current_version,
            std::string state_path = default_state_path());

    const std::string& current_version() const { return current_; }
    const State& state() const { return state_; }

    // Automatic checks: off for dev builds, with $GSYSTEMCTL_NO_UPDATE, or
    // when turned off in the state file. Says why in `reason` when off.
    bool checks_enabled(std::string* reason = nullptr) const;
    // Whether a check is due (none in the last day).
    bool check_due(long long now) const;
    // Asks GitHub for the latest release and records the check. Returns it if
    // newer than this build; `error` explains a failed check.
    std::optional<Release> check(std::string* error);
    // The newest release seen by an earlier check, if newer than this build and not skipped.
    std::optional<Release> cached_update() const;
    void skip(const std::string& tag);
    void disable_checks();

    // How to update this install to `release`.
    Plan plan(const Release& release) const;
    // Downloads, verifies and installs. With `interactive`, sudo may prompt on
    // the terminal (CLI); otherwise `password` is passed to sudo -S when needed.
    Result install(const Release& release, const Plan& plan, bool interactive, const std::string& password = "");

    // $XDG_CONFIG_HOME/g-systemctl/update, else ~/.config/...; empty if neither is set.
    static std::string default_state_path();
    // Real path of the running binary; empty if unknown.
    static std::string executable_path();

    // Exposed for tests.
    static std::string parse_latest_tag(const std::string& json);
    // Hex digest for `asset` in `sha256sum` output; empty if missing.
    static std::string find_checksum(const std::string& sums, const std::string& asset);
    static State parse_state(const std::string& content);
    static std::string serialize_state(const State& state);
    // `uname -m` -> release arch ("x86_64", "aarch64"); empty if no build exists.
    static std::string linux_arch(const std::string& machine);
    // Plan for a binary at `target`. `os` is "linux" or "macos".
    static Plan make_plan(const std::string& os, const std::string& machine, const std::string& target,
                          bool from_deb, bool writable);
    static std::string download_url(const std::string& tag, const std::string& asset);

private:
    std::shared_ptr<CommandExecutor> executor_;
    std::string current_;
    std::string state_path_;
    State state_;

    void save_state();
    // curl or wget; `max_seconds` bounds the whole transfer.
    CommandResult fetch(const std::string& url, const std::string& output, int max_seconds,
                        bool github_api = false);
    std::string sha256(const std::string& file);
    CommandResult run(const std::string& command, bool root, bool interactive, const std::string& password);
};

} // namespace gsystemctl
