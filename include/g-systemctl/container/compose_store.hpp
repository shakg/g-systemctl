#pragma once

#include "g-systemctl/container/container.hpp"
#include <string>
#include <vector>

namespace gsystemctl {

// Compose projects seen before, so a project stays in the list (and can be
// brought up again) after `docker compose down` removed its containers.
// Stored one per line as "name<TAB>working_dir<TAB>file1,file2".
class ComposeStore {
public:
    // Empty path: keep projects in memory only.
    explicit ComposeStore(std::string path);

    // $XDG_CONFIG_HOME/g-systemctl/compose-projects, else ~/.config/...; empty if neither is set.
    static std::string default_path();

    const std::vector<ComposeProjectRef>& projects() const { return projects_; }
    // Adds or updates a project; writes the file only when something changed.
    // Refs without a working directory are ignored. Returns true if changed.
    bool remember(const ComposeProjectRef& ref);
    // Returns true if the project was saved.
    bool forget(const std::string& name);
    // Last write error, empty if none.
    const std::string& error() const { return error_; }

    // Exposed for tests.
    static std::vector<ComposeProjectRef> parse(const std::string& content);
    static std::string serialize(const std::vector<ComposeProjectRef>& projects);

private:
    std::string path_;
    std::vector<ComposeProjectRef> projects_;
    std::string error_;

    void save();
};

} // namespace gsystemctl
