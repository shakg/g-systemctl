#include <gtest/gtest.h>
#include <g-systemctl/container/compose_store.hpp>
#include <g-systemctl/container/cli_container_manager.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace gsystemctl {

namespace {

Container member(const std::string& id, const std::string& project, const std::string& service, int number,
                 const std::string& state = "running", const std::string& hash = "") {
    Container c;
    c.id = id;
    c.name = project + "-" + service + "-" + std::to_string(number);
    c.state = state;
    c.compose_project = project;
    c.compose_service = service;
    c.compose_number = number;
    c.compose_working_dir = "/srv/" + project;
    c.compose_config_files = "/srv/" + project + "/compose.yaml";
    c.compose_config_hash = hash;
    return c;
}

std::string temp_dir() {
    char path[] = "/tmp/gsystemctl-compose-XXXXXX";
    return mkdtemp(path);
}

} // namespace

TEST(ComposeTest, ParsesComposeLabelsFromPs) {
    const std::string output =
        "aa\tshop-web-1\tnginx\trunning\tUp 1 minute\t\tshop\tweb\t1\t/srv/shop\t/srv/shop/compose.yaml,/srv/shop/"
        "override.yaml\t0df6\tFalse\tapi:service_started:false,cache:service_healthy:true\n"
        "bb\tshop-api-run-1\talpine\texited\tExited (0)\t\tshop\tapi\t1\t/srv/shop\t/srv/shop/compose.yaml\t9894\tTrue\t\n"
        "cc\tlone\talpine\trunning\tUp\t\t\t\t\t\t\t\t\t\n";
    auto containers = CliContainerManager::parse_ps_output(output);
    ASSERT_EQ(containers.size(), 3u);
    const auto& lone = containers[0];
    EXPECT_EQ(lone.name, "lone");
    EXPECT_TRUE(lone.compose_project.empty());
    EXPECT_EQ(lone.compose_number, 0);

    const auto& web = containers[1];
    EXPECT_EQ(web.compose_service, "web");
    EXPECT_EQ(web.compose_number, 1);
    EXPECT_EQ(web.compose_working_dir, "/srv/shop");
    EXPECT_EQ(web.compose_config_hash, "0df6");
    EXPECT_FALSE(web.compose_oneoff);
    EXPECT_EQ(web.compose_depends_on, (std::vector<std::string>{"api", "cache"}));
    EXPECT_EQ(split_compose_files(web.compose_config_files),
              (std::vector<std::string>{"/srv/shop/compose.yaml", "/srv/shop/override.yaml"}));

    EXPECT_TRUE(containers[2].compose_oneoff);
}

TEST(ComposeTest, OldPsOutputWithoutComposeLabelsStillParses) {
    auto containers = CliContainerManager::parse_ps_output("aa\tweb\tnginx\trunning\tUp\t\tshop\n");
    ASSERT_EQ(containers.size(), 1u);
    EXPECT_EQ(containers[0].compose_project, "shop");
    EXPECT_TRUE(containers[0].compose_service.empty());
}

TEST(ComposeTest, ParsesConfigHashes) {
    const std::string hash(64, 'a');
    auto hashes = parse_compose_hashes("WARN[0000] something happened\napi " + hash + "\nweb " + std::string(64, 'b') +
                                       "\n\n");
    ASSERT_EQ(hashes.size(), 2u);
    EXPECT_EQ(hashes["api"], hash);
}

TEST(ComposeTest, ProjectNameFromDirectory) {
    EXPECT_EQ(compose_project_name_from_dir("/srv/My Shop/"), "myshop");
    EXPECT_EQ(compose_project_name_from_dir("/home/me/_api.v2"), "apiv2");
    EXPECT_EQ(compose_project_name_from_dir("relative"), "relative");
}

TEST(ComposeTest, GroupsProjectsAndCountsReplicas) {
    std::vector<Container> containers = {
        member("w2", "shop", "worker", 2, "exited"),
        member("w1", "shop", "worker", 1),
        member("a1", "shop", "api", 1),
        member("m1", "mon", "grafana", 1, "paused"),
    };
    auto oneoff = member("r1", "shop", "api", 1, "exited");
    oneoff.compose_oneoff = true;
    containers.push_back(oneoff);
    Container lone;
    lone.id = "x";
    lone.name = "lone";
    lone.state = "running";
    containers.push_back(lone);

    ComposeProjectRef saved{"old", "/srv/old", {"/srv/old/compose.yaml"}};
    auto projects = group_compose_projects(containers, {saved, {"shop", "/elsewhere", {}}});
    ASSERT_EQ(projects.size(), 3u);

    EXPECT_EQ(projects[0].ref.name, "mon");
    EXPECT_EQ(projects[0].active, 1);
    EXPECT_EQ(projects[0].running, 0);

    EXPECT_EQ(projects[1].ref.name, "old");
    EXPECT_TRUE(projects[1].down());
    EXPECT_EQ(projects[1].ref.working_dir, "/srv/old");

    const auto& shop = projects[2];
    EXPECT_EQ(shop.ref.working_dir, "/srv/shop"); // labels win over the saved entry
    EXPECT_EQ(shop.total, 3);
    EXPECT_EQ(shop.running, 2);
    EXPECT_EQ(shop.container_ids, (std::vector<std::string>{"a1", "w1", "w2", "r1"}));
    EXPECT_EQ(shop.services, (std::vector<std::string>{"api", "worker"}));
}

TEST(ComposeTest, FindsDriftedServices) {
    std::vector<Container> containers = {
        member("a", "shop", "api", 1, "running", "h1"),
        member("w", "shop", "web", 1, "running", "h2"),
        member("o", "other", "web", 1, "running", "zz"),
        member("n", "shop", "new", 1, "running", ""),
    };
    auto drifted = compose_drifted_services(containers, "shop", {{"api", "h1"}, {"web", "changed"}});
    EXPECT_EQ(drifted, (std::vector<std::string>{"web"}));
}

TEST(ComposeTest, ActionArgs) {
    EXPECT_EQ(compose_action_args(ComposeAction::Up), (std::vector<std::string>{"up", "-d"}));
    EXPECT_EQ(compose_action_args(ComposeAction::UpBuild), (std::vector<std::string>{"up", "-d", "--build"}));
    EXPECT_EQ(compose_action_args(ComposeAction::Down), (std::vector<std::string>{"down"}));
}

TEST(ComposeTest, BuildsComposeCommand) {
    ComposeProjectRef ref{"shop", "/srv/my shop", {"/srv/my shop/compose.yaml", "/srv/my shop/dev.yaml"}};
    EXPECT_EQ(CliContainerManager::build_compose_command({"docker", "compose"}, ref, {"up", "-d"}, true),
              "cd '/srv/my shop' && 'docker' 'compose' '-p' 'shop' '-f' '/srv/my shop/compose.yaml' '-f' "
              "'/srv/my shop/dev.yaml' 'up' '-d'");
    EXPECT_EQ(CliContainerManager::build_compose_command({"docker-compose"}, ref, {"stop"}, false),
              "'docker-compose' '-p' 'shop' 'stop'");
}

TEST(ComposeTest, ParsesProjectNameFromConfig) {
    EXPECT_EQ(CliContainerManager::parse_config_project_name("name: shop\nservices:\n  web:\n    name: x\n"), "shop");
    EXPECT_EQ(CliContainerManager::parse_config_project_name("name: \"my-app\"\n"), "my-app");
    EXPECT_EQ(CliContainerManager::parse_config_project_name("services: {}\n"), "");
    ComposeProjectRef files{"", "/srv/shop", {}};
    EXPECT_EQ(CliContainerManager::build_compose_command({"docker", "compose"}, files, {"config"}, true),
              "cd '/srv/shop' && 'docker' 'compose' 'config'");
}

TEST(ComposeTest, BuildsComposeLogsArgv) {
    ComposeProjectRef ref{"shop", "/srv/shop", {}};
    EXPECT_EQ(CliContainerManager::build_compose_logs_argv({"docker", "compose"}, ref, 100, "123.5"),
              (std::vector<std::string>{"docker", "compose", "-p", "shop", "logs", "-f", "--no-color", "--tail",
                                        "100", "--since", "123.5"}));
}

TEST(ComposeStoreTest, RoundTripsAndPersists) {
    auto dir = temp_dir();
    auto path = dir + "/nested/compose-projects";
    {
        ComposeStore store(path);
        EXPECT_TRUE(store.remember({"shop", "/srv/shop", {"/srv/shop/a.yaml", "/srv/shop/b.yaml"}}));
        EXPECT_FALSE(store.remember({"shop", "/srv/shop", {"/srv/shop/a.yaml", "/srv/shop/b.yaml"}}));
        EXPECT_TRUE(store.remember({"api", "/srv/api", {}}));
        EXPECT_FALSE(store.remember({"nodir", "", {}}));
        EXPECT_TRUE(store.error().empty()) << store.error();
    }
    {
        ComposeStore store(path);
        ASSERT_EQ(store.projects().size(), 2u);
        EXPECT_EQ(store.projects()[0].name, "api");
        EXPECT_EQ(store.projects()[1].config_files.size(), 2u);
        EXPECT_TRUE(store.forget("api"));
        EXPECT_FALSE(store.forget("api"));
    }
    ComposeStore store(path);
    EXPECT_EQ(store.projects().size(), 1u);
    std::remove(path.c_str());
}

TEST(ComposeStoreTest, IgnoresCommentsAndBadLines) {
    auto projects = ComposeStore::parse("# header\nbad line\nshop\t/srv/shop\t\n\t/x\t\n");
    ASSERT_EQ(projects.size(), 1u);
    EXPECT_EQ(projects[0].name, "shop");
    EXPECT_TRUE(projects[0].config_files.empty());
}

} // namespace gsystemctl
