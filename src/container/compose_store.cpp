#include "g-systemctl/container/compose_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace gsystemctl {

namespace {

// mkdir -p; ignores errors (the later write reports them).
void make_dirs(const std::string& path) {
    for (size_t pos = 1; pos != std::string::npos; ) {
        pos = path.find('/', pos + 1);
        ::mkdir(path.substr(0, pos).c_str(), 0755);
    }
}

} // namespace

ComposeStore::ComposeStore(std::string path) : path_(std::move(path)) {
    if (path_.empty()) {
        return;
    }
    std::ifstream in(path_);
    if (in) {
        std::stringstream buffer;
        buffer << in.rdbuf();
        projects_ = parse(buffer.str());
    }
}

std::string ComposeStore::default_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/g-systemctl/compose-projects";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/g-systemctl/compose-projects";
    }
    return "";
}

std::vector<ComposeProjectRef> ComposeStore::parse(const std::string& content) {
    std::vector<ComposeProjectRef> projects;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) {
            continue;
        }
        auto tab2 = line.find('\t', tab1 + 1);
        ComposeProjectRef ref;
        ref.name = line.substr(0, tab1);
        ref.working_dir = line.substr(tab1 + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab1 - 1);
        if (tab2 != std::string::npos) {
            ref.config_files = split_compose_files(line.substr(tab2 + 1));
        }
        if (!ref.name.empty() && !ref.working_dir.empty()) {
            projects.push_back(std::move(ref));
        }
    }
    return projects;
}

std::string ComposeStore::serialize(const std::vector<ComposeProjectRef>& projects) {
    std::string out = "# Compose projects known to g-systemctl: name, working dir, config files\n";
    for (const auto& p : projects) {
        out += p.name + "\t" + p.working_dir + "\t";
        for (size_t i = 0; i < p.config_files.size(); ++i) {
            out += (i ? "," : "") + p.config_files[i];
        }
        out += "\n";
    }
    return out;
}

bool ComposeStore::remember(const ComposeProjectRef& ref) {
    if (ref.name.empty() || ref.working_dir.empty()) {
        return false;
    }
    auto it = std::find_if(projects_.begin(), projects_.end(),
                           [&ref](const ComposeProjectRef& p) { return p.name == ref.name; });
    if (it != projects_.end()) {
        if (*it == ref) {
            return false;
        }
        *it = ref;
    } else {
        projects_.push_back(ref);
        std::sort(projects_.begin(), projects_.end(),
                  [](const ComposeProjectRef& a, const ComposeProjectRef& b) { return a.name < b.name; });
    }
    save();
    return true;
}

bool ComposeStore::forget(const std::string& name) {
    auto it = std::find_if(projects_.begin(), projects_.end(),
                           [&name](const ComposeProjectRef& p) { return p.name == name; });
    if (it == projects_.end()) {
        return false;
    }
    projects_.erase(it);
    save();
    return true;
}

void ComposeStore::save() {
    if (path_.empty()) {
        return;
    }
    auto slash = path_.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        make_dirs(path_.substr(0, slash));
    }
    // Write a temp file and rename, so a crash never leaves a half-written list.
    const auto tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            error_ = "Cannot write " + tmp + ": " + std::strerror(errno);
            return;
        }
        out << serialize(projects_);
        if (!out) {
            error_ = "Cannot write " + tmp;
            return;
        }
    }
    if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
        error_ = "Cannot write " + path_ + ": " + std::strerror(errno);
        return;
    }
    error_.clear();
}

} // namespace gsystemctl
