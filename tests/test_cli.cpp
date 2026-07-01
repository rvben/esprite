#include "doctest.h"
#include "cli.h"
#include <vector>
#include <string>
#include <initializer_list>

static int run_cli(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    static std::vector<std::string> storage;
    storage.clear();
    for (auto* a : args) storage.emplace_back(a);
    for (auto& s : storage) argv.push_back(const_cast<char*>(s.c_str()));
    return esp32sim_main((int)argv.size(), argv.data());
}

TEST_CASE("schema, help, and list-targets succeed without a booted target") {
    CHECK(run_cli({"esp32sim", "schema"}) == 0);
    CHECK(run_cli({"esp32sim", "--help"}) == 0);
    CHECK(run_cli({"esp32sim", "list-targets"}) == 0);
}

TEST_CASE("no arguments is a usage error") {
    CHECK(run_cli({"esp32sim"}) == 1);
}
