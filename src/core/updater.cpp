#include "g-systemctl/core/updater.hpp"
#include "g-systemctl/core/shell.hpp"
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <glob.h>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace gsystemctl {

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string first_line(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (!line.empty()) {
            return line;
        }
    }
    return "";
}

std::string failure(const std::string& what, const CommandResult& result) {
    auto line = first_line(result.stdout_output + result.stderr_output);
    return what + (line.empty() ? " (exit code " + std::to_string(result.exit_code) + ")" : ": " + line);
}

void make_dirs(const std::string& path) {
    std::error_code ignored;
    std::filesystem::create_directories(path, ignored);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Removes the temporary download directory when the install returns.
struct TempDir {
    std::string path;
    ~TempDir() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    }
};

} // namespace

std::optional<std::array<int, 3>> parse_release_version(const std::string& version) {
    std::string v = version;
    if (!v.empty() && v[0] == 'v') {
        v.erase(0, 1);
    }
    std::array<int, 3> parts{};
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        if (pos >= v.size() || !std::isdigit(static_cast<unsigned char>(v[pos]))) {
            return std::nullopt;
        }
        long value = 0;
        while (pos < v.size() && std::isdigit(static_cast<unsigned char>(v[pos]))) {
            value = value * 10 + (v[pos] - '0');
            if (value > INT_MAX / 10) {
                return std::nullopt;
            }
            ++pos;
        }
        parts[i] = static_cast<int>(value);
        if (i < 2) {
            if (pos >= v.size() || v[pos] != '.') {
                return std::nullopt;
            }
            ++pos;
        }
    }
    if (pos != v.size()) {
        return std::nullopt; // "-dev", "-3-gabc1234", ...
    }
    return parts;
}

bool is_newer_release(const std::string& latest, const std::string& current) {
    auto l = parse_release_version(latest);
    auto c = parse_release_version(current);
    return l && c && *l > *c;
}

Updater::Updater(std::shared_ptr<CommandExecutor> executor, std::string current_version, std::string state_path)
    : executor_(std::move(executor)), current_(std::move(current_version)), state_path_(std::move(state_path)) {
    if (!state_path_.empty()) {
        state_ = parse_state(read_file(state_path_));
    }
}

std::string Updater::default_state_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/g-systemctl/update";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/g-systemctl/update";
    }
    return "";
}

std::string Updater::executable_path() {
    char buffer[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return "";
    }
    char resolved[PATH_MAX];
    return realpath(buffer, resolved) ? std::string(resolved) : std::string(buffer);
#else
    auto length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return "";
    }
    buffer[length] = '\0';
    return buffer;
#endif
}

bool Updater::checks_enabled(std::string* reason) const {
    auto off = [reason](const std::string& why) {
        if (reason) {
            *reason = why;
        }
        return false;
    };
    const char* env = std::getenv("GSYSTEMCTL_NO_UPDATE");
    if (env && *env && std::string(env) != "0") {
        return off("GSYSTEMCTL_NO_UPDATE is set");
    }
    if (!state_.check) {
        return off("turned off in " + state_path_);
    }
    if (!parse_release_version(current_)) {
        return off("development build " + current_);
    }
    return true;
}

bool Updater::check_due(long long now) const {
    return now - state_.last_check >= kCheckInterval || now < state_.last_check;
}

std::optional<Updater::Release> Updater::check(std::string* error) {
    auto result = fetch(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest", "", 10, true);
    state_.last_check = static_cast<long long>(std::time(nullptr));
    if (result.exit_code != 0) {
        save_state();
        if (error) {
            *error = failure("Update check failed", result);
        }
        return std::nullopt;
    }
    auto tag = parse_latest_tag(result.stdout_output);
    if (tag.empty() || !parse_release_version(tag)) {
        save_state();
        if (error) {
            *error = "Update check failed: no release tag in the GitHub response";
        }
        return std::nullopt;
    }
    state_.latest = tag;
    save_state();
    if (!is_newer_release(tag, current_)) {
        return std::nullopt;
    }
    return Release{tag, tag.substr(tag[0] == 'v' ? 1 : 0)};
}

std::optional<Updater::Release> Updater::cached_update() const {
    const auto& tag = state_.latest;
    if (tag.empty() || tag == state_.skip || !is_newer_release(tag, current_)) {
        return std::nullopt;
    }
    return Release{tag, tag.substr(tag[0] == 'v' ? 1 : 0)};
}

void Updater::skip(const std::string& tag) {
    state_.skip = tag;
    save_state();
}

void Updater::disable_checks() {
    state_.check = false;
    save_state();
}

void Updater::save_state() {
    if (state_path_.empty()) {
        return;
    }
    auto slash = state_path_.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        make_dirs(state_path_.substr(0, slash));
    }
    const auto tmp = state_path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            return;
        }
        out << serialize_state(state_);
    }
    std::rename(tmp.c_str(), state_path_.c_str());
}

std::string Updater::parse_latest_tag(const std::string& json) {
    // {"url": ..., "tag_name": "v2.3.0", ...}
    const std::string key = "\"tag_name\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find(':', pos + key.size());
    if (pos == std::string::npos) {
        return "";
    }
    auto open = json.find('"', pos);
    if (open == std::string::npos) {
        return "";
    }
    auto close = json.find('"', open + 1);
    if (close == std::string::npos) {
        return "";
    }
    return json.substr(open + 1, close - open - 1);
}

std::string Updater::find_checksum(const std::string& sums, const std::string& asset) {
    // "<hex>  <name>" or "<hex> *<name>" (binary mode).
    std::istringstream stream(sums);
    std::string line;
    while (std::getline(stream, line)) {
        std::istringstream fields(line);
        std::string digest, name;
        if (!(fields >> digest >> name)) {
            continue;
        }
        if (!name.empty() && name[0] == '*') {
            name.erase(0, 1);
        }
        if (name == asset && digest.size() == 64) {
            return digest;
        }
    }
    return "";
}

Updater::State Updater::parse_state(const std::string& content) {
    State state;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        auto key = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        if (key == "check") {
            state.check = !(value == "off" || value == "false" || value == "no" || value == "0");
        } else if (key == "last-check") {
            state.last_check = std::atoll(value.c_str());
        } else if (key == "latest") {
            state.latest = value;
        } else if (key == "skip") {
            state.skip = value;
        }
    }
    return state;
}

std::string Updater::serialize_state(const State& state) {
    std::string out = "# g-systemctl update checks. Set check=off to stop them.\n";
    out += std::string("check=") + (state.check ? "on" : "off") + "\n";
    out += "last-check=" + std::to_string(state.last_check) + "\n";
    if (!state.latest.empty()) {
        out += "latest=" + state.latest + "\n";
    }
    if (!state.skip.empty()) {
        out += "skip=" + state.skip + "\n";
    }
    return out;
}

std::string Updater::linux_arch(const std::string& machine) {
    if (machine == "x86_64" || machine == "amd64") {
        return "x86_64";
    }
    if (machine == "aarch64" || machine == "arm64") {
        return "aarch64";
    }
    return "";
}

std::string Updater::download_url(const std::string& tag, const std::string& asset) {
    return std::string("https://github.com/") + kRepo + "/releases/download/" + tag + "/" + asset;
}

Updater::Plan Updater::make_plan(const std::string& os, const std::string& machine, const std::string& target,
                                 bool from_deb, bool writable) {
    Plan plan;
    plan.target = target;
    if (target.empty()) {
        plan.error = "Cannot find the g-systemctl binary to replace";
        return plan;
    }
    if (target.find("/Cellar/") != std::string::npos) {
        plan.kind = InstallKind::Homebrew;
        plan.error = "Installed with Homebrew: reinstall from the latest g-systemctl.rb (see the README)";
        return plan;
    }
    if (os == "macos") {
        if (machine != "arm64") {
            plan.error = "No prebuilt binary for " + machine + " Macs; build from source (see the README)";
            return plan;
        }
        plan.asset = "g-systemctl-macos.tar.gz";
        plan.sums_asset = "SHA256SUMS-macos.txt";
        plan.needs_root = !writable;
        return plan;
    }
    const auto arch = linux_arch(machine);
    if (os != "linux" || arch.empty()) {
        plan.error = "No prebuilt binary for " + os + " " + machine + "; build from source (see the README)";
        return plan;
    }
    plan.sums_asset = "SHA256SUMS-linux-" + arch + ".txt";
    if (from_deb) {
        plan.kind = InstallKind::Deb;
        plan.asset = std::string("g-systemctl-linux-") + (arch == "x86_64" ? "amd64" : "arm64") + ".deb";
        plan.needs_root = true;
        return plan;
    }
    plan.asset = "g-systemctl-linux-" + arch + ".tar.gz";
    plan.needs_root = !writable;
    return plan;
}

Updater::Plan Updater::plan(const Release&) const {
    struct utsname info {};
    std::string machine = uname(&info) == 0 ? info.machine : "";
#ifdef __APPLE__
    const std::string os = "macos";
#else
    const std::string os = "linux";
#endif
    const auto target = executable_path();
    bool from_deb = false;
    bool writable = false;
    if (!target.empty()) {
        auto dir = target.substr(0, target.find_last_of('/'));
        writable = access(dir.empty() ? "/" : dir.c_str(), W_OK) == 0;
        if (os == "linux") {
            from_deb = executor_->execute("dpkg -S " + shell_quote(target) + " >/dev/null").exit_code == 0;
        }
    }
    return make_plan(os, machine, target, from_deb, writable);
}

CommandResult Updater::fetch(const std::string& url, const std::string& output, int max_seconds, bool github_api) {
    const bool curl = executor_->execute("command -v curl >/dev/null").exit_code == 0;
    std::string command;
    if (curl) {
        command = "curl -fsSL --max-time " + std::to_string(max_seconds);
        if (github_api) {
            command += " -H 'Accept: application/vnd.github+json'";
        }
        command += " -o " + shell_quote(output.empty() ? "-" : output) + " " + shell_quote(url);
    } else {
        command = "wget -q -T " + std::to_string(max_seconds) + " -O " + shell_quote(output.empty() ? "-" : output) +
                  " " + shell_quote(url);
    }
    return executor_->execute(command);
}

std::string Updater::sha256(const std::string& file) {
    for (const std::string tool : {"sha256sum", "shasum -a 256"}) {
        auto result = executor_->execute(tool + " " + shell_quote(file));
        if (result.exit_code == 0) {
            std::istringstream fields(result.stdout_output);
            std::string digest;
            fields >> digest;
            if (digest.size() == 64) {
                return digest;
            }
        }
    }
    return "";
}

CommandResult Updater::run(const std::string& command, bool root, bool interactive, const std::string& password) {
    // sudo applies to one command, so the whole line runs in its own shell.
    const auto line = "sh -c " + shell_quote(command);
    if (root && interactive) {
        return {executor_->execute_interactive(line, true), "", ""};
    }
    if (root) {
        return executor_->execute_privileged(line, password);
    }
    return executor_->execute(line);
}

Updater::Result Updater::install(const Release& release, const Plan& plan, bool interactive,
                                 const std::string& password) {
    if (!plan.error.empty()) {
        return {false, plan.error};
    }

    const char* tmp_root = std::getenv("TMPDIR");
    std::string pattern = std::string(tmp_root && *tmp_root ? tmp_root : "/tmp") + "/g-systemctl-update.XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (!mkdtemp(buffer.data())) {
        return {false, std::string("Cannot create a temporary directory: ") + std::strerror(errno)};
    }
    TempDir dir{buffer.data()};

    const auto asset = dir.path + "/" + plan.asset;
    const auto sums = dir.path + "/" + plan.sums_asset;
    auto result = fetch(download_url(release.tag, plan.asset), asset, 300);
    if (result.exit_code != 0) {
        return {false, failure("Download of " + plan.asset + " failed", result)};
    }
    result = fetch(download_url(release.tag, plan.sums_asset), sums, 60);
    if (result.exit_code != 0) {
        return {false, failure("Download of " + plan.sums_asset + " failed", result)};
    }
    const auto expected = find_checksum(read_file(sums), plan.asset);
    if (expected.empty()) {
        return {false, plan.sums_asset + " has no checksum for " + plan.asset};
    }
    const auto actual = sha256(asset);
    if (actual.empty()) {
        return {false, "Cannot compute a checksum: neither sha256sum nor shasum works"};
    }
    if (actual != expected) {
        return {false, "Checksum mismatch for " + plan.asset + "; nothing was installed"};
    }

    auto verify = [this, &release](const std::string& binary) -> Result {
        auto out = executor_->execute(shell_quote(binary) + " --version");
        if (out.exit_code != 0 || out.stdout_output.find(release.version) == std::string::npos) {
            return {false, failure("The new binary does not run", out)};
        }
        return {true, ""};
    };

    if (plan.kind == InstallKind::Deb) {
        result = run("dpkg -i " + shell_quote(asset), true, interactive, password);
        if (result.exit_code != 0) {
            return {false, failure("dpkg -i failed", result)};
        }
        auto ok = verify(plan.target);
        return ok.first ? Result{true, "Updated to " + release.version} : ok;
    }

    result = executor_->execute("tar -xzf " + shell_quote(asset) + " -C " + shell_quote(dir.path));
    if (result.exit_code != 0) {
        return {false, failure("Cannot unpack " + plan.asset, result)};
    }
    glob_t found{};
    const auto glob_pattern = dir.path + "/g-systemctl-*/g-systemctl";
    std::string binary;
    if (glob(glob_pattern.c_str(), 0, nullptr, &found) == 0 && found.gl_pathc > 0) {
        binary = found.gl_pathv[0];
    }
    globfree(&found);
    if (binary.empty()) {
        return {false, plan.asset + " has no g-systemctl binary"};
    }
    auto ok = verify(binary);
    if (!ok.first) {
        return ok;
    }

    // Copy next to the target, then rename over it: atomic, and the running
    // process keeps its old file.
    const auto staged = plan.target + ".new";
    result = run("install -m 755 " + shell_quote(binary) + " " + shell_quote(staged) + " && mv -f " +
                     shell_quote(staged) + " " + shell_quote(plan.target),
                 plan.needs_root, interactive, password);
    if (result.exit_code != 0) {
        return {false, failure("Cannot replace " + plan.target, result)};
    }
    return {true, "Updated to " + release.version};
}

} // namespace gsystemctl
