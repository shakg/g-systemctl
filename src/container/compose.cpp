#include "g-systemctl/container/container.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace gsystemctl {

namespace {

std::string trim(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return begin < end ? std::string(begin, end) : std::string();
}

std::vector<std::string> split(const std::string& value, char sep) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(value);
    while (std::getline(stream, part, sep)) {
        part = trim(part);
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

} // namespace

std::vector<std::string> compose_action_args(ComposeAction action) {
    switch (action) {
    case ComposeAction::Start: return {"start"};
    case ComposeAction::Stop: return {"stop"};
    case ComposeAction::Restart: return {"restart"};
    case ComposeAction::Up: return {"up", "-d"};
    case ComposeAction::UpBuild: return {"up", "-d", "--build"};
    case ComposeAction::Pull: return {"pull"};
    case ComposeAction::Down: return {"down"};
    }
    return {};
}

std::string compose_action_verb(ComposeAction action) {
    switch (action) {
    case ComposeAction::Start: return "Starting";
    case ComposeAction::Stop: return "Stopping";
    case ComposeAction::Restart: return "Restarting";
    case ComposeAction::Up: return "Bringing up";
    case ComposeAction::UpBuild: return "Building and bringing up";
    case ComposeAction::Pull: return "Pulling images for";
    case ComposeAction::Down: return "Taking down";
    }
    return "";
}

std::string compose_action_past(ComposeAction action) {
    switch (action) {
    case ComposeAction::Start: return "Started";
    case ComposeAction::Stop: return "Stopped";
    case ComposeAction::Restart: return "Restarted";
    case ComposeAction::Up: return "Brought up";
    case ComposeAction::UpBuild: return "Built and brought up";
    case ComposeAction::Pull: return "Pulled images for";
    case ComposeAction::Down: return "Took down";
    }
    return "";
}

std::vector<std::string> parse_compose_depends_on(const std::string& label) {
    std::vector<std::string> services;
    for (const auto& entry : split(label, ',')) {
        auto name = trim(entry.substr(0, entry.find(':')));
        if (!name.empty() && std::find(services.begin(), services.end(), name) == services.end()) {
            services.push_back(name);
        }
    }
    return services;
}

std::vector<std::string> split_compose_files(const std::string& label) { return split(label, ','); }

std::map<std::string, std::string> parse_compose_hashes(const std::string& output) {
    std::map<std::string, std::string> hashes;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        auto fields = split(line, ' ');
        // Anything else (warnings) does not have exactly "service hash".
        if (fields.size() == 2 && fields[1].size() >= 32 &&
            std::all_of(fields[1].begin(), fields[1].end(), [](unsigned char c) { return std::isxdigit(c); })) {
            hashes[fields[0]] = fields[1];
        }
    }
    return hashes;
}

std::string compose_project_name_from_dir(const std::string& dir) {
    auto path = trim(dir);
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    auto slash = path.find_last_of('/');
    auto base = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string name;
    for (unsigned char c : base) {
        c = static_cast<unsigned char>(std::tolower(c));
        if (std::isalnum(c) || c == '-' || c == '_') {
            name += static_cast<char>(c);
        }
    }
    // Compose requires the name to start with a letter or digit.
    auto first = name.find_first_not_of("-_");
    return first == std::string::npos ? "" : name.substr(first);
}

std::vector<ComposeProject> group_compose_projects(const std::vector<Container>& containers,
                                                   const std::vector<ComposeProjectRef>& saved) {
    std::map<std::string, ComposeProject> projects;
    std::map<std::string, std::vector<const Container*>> members;
    for (const auto& c : containers) {
        if (c.compose_project.empty()) {
            continue;
        }
        auto& p = projects[c.compose_project];
        p.ref.name = c.compose_project;
        // A project may mix containers from older runs; prefer a running one's labels.
        if (p.ref.working_dir.empty() || (c.is_running() && !c.compose_oneoff)) {
            if (!c.compose_working_dir.empty()) {
                p.ref.working_dir = c.compose_working_dir;
            }
            auto files = split_compose_files(c.compose_config_files);
            if (!files.empty()) {
                p.ref.config_files = files;
            }
        }
        members[c.compose_project].push_back(&c);
        if (!c.compose_oneoff) {
            ++p.total;
            p.running += c.is_running() ? 1 : 0;
            p.active += c.is_active() ? 1 : 0;
        }
    }
    for (auto& [name, list] : members) {
        std::sort(list.begin(), list.end(), [](const Container* a, const Container* b) {
            if (a->compose_oneoff != b->compose_oneoff)
                return !a->compose_oneoff;
            if (a->compose_service != b->compose_service)
                return a->compose_service < b->compose_service;
            if (a->compose_number != b->compose_number)
                return a->compose_number < b->compose_number;
            return a->name < b->name;
        });
        auto& p = projects[name];
        std::set<std::string> services;
        for (const auto* c : list) {
            p.container_ids.push_back(c->id);
            if (!c->compose_service.empty() && !c->compose_oneoff) {
                services.insert(c->compose_service);
            }
        }
        p.services.assign(services.begin(), services.end());
    }
    for (const auto& ref : saved) {
        if (ref.name.empty()) {
            continue;
        }
        auto& p = projects[ref.name];
        if (p.ref.name.empty()) {
            p.ref = ref;
        } else if (p.ref.working_dir.empty()) {
            p.ref.working_dir = ref.working_dir;
            if (p.ref.config_files.empty()) {
                p.ref.config_files = ref.config_files;
            }
        }
    }
    std::vector<ComposeProject> result;
    result.reserve(projects.size());
    for (auto& [name, p] : projects) {
        result.push_back(std::move(p));
    }
    return result;
}

std::vector<std::string> compose_drifted_services(const std::vector<Container>& containers,
                                                  const std::string& project,
                                                  const std::map<std::string, std::string>& current) {
    std::set<std::string> drifted;
    for (const auto& c : containers) {
        if (c.compose_project != project || c.compose_oneoff || c.compose_config_hash.empty()) {
            continue;
        }
        auto it = current.find(c.compose_service);
        if (it != current.end() && it->second != c.compose_config_hash) {
            drifted.insert(c.compose_service);
        }
    }
    return {drifted.begin(), drifted.end()};
}

} // namespace gsystemctl
