#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/library/machine_store.hpp>
#include <seventhslide/model/machine_settings.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using seventhslide::library::MachineStore;
using seventhslide::model::MachineSettings;
using nlohmann::json;

TEST_CASE("MachineStore gives a computer one id and keeps it", "[library][machine]")
{
    const seventhslide::test::TempDir tmp;
    const auto data_dir = tmp.path / "data";  // made by the first write

    MachineStore first(data_dir);
    first.load();
    const std::string id = first.machine_id();
    REQUIRE(id.size() == 32);
    CHECK(id.find_first_not_of("0123456789abcdef") == std::string::npos);
    // Written at once, before anything else is saved.
    REQUIRE(std::filesystem::exists(first.file()));
    CHECK_FALSE(first.settings().has_value());

    MachineStore again(data_dir);
    again.load();
    CHECK(again.machine_id() == id);

    // Another computer (another data dir) is another id.
    MachineStore other(tmp.path / "other");
    other.load();
    CHECK(other.machine_id() != id);
}

TEST_CASE("MachineStore keeps the settings it is given", "[library][machine]")
{
    const seventhslide::test::TempDir tmp;
    MachineStore store(tmp.path);
    store.load();
    const std::string id = store.machine_id();

    MachineSettings s;
    s.remote_enabled = true;
    s.remote_pin = "2468";
    s.remote_port = 9000;
    REQUIRE(store.save_settings(s.to_json()));
    REQUIRE(store.settings().has_value());

    MachineStore reread(tmp.path);
    reread.load();
    CHECK(reread.machine_id() == id);
    REQUIRE(reread.settings().has_value());
    CHECK(MachineSettings::from_json(*reread.settings()) == s);
}

TEST_CASE("MachineStore reads a damaged file as a new computer", "[library][machine]")
{
    const seventhslide::test::TempDir tmp;
    MachineStore store(tmp.path);
    {
        std::ofstream out(store.file());
        out << "{ not json";
    }
    store.load();
    CHECK(store.machine_id().size() == 32);
    CHECK_FALSE(store.settings().has_value());

    // An id that is not one of ours is replaced too.
    {
        std::ofstream out(store.file());
        out << json{{"version", 1}, {"machine_id", "church-pc"}}.dump();
    }
    store.load();
    CHECK(store.machine_id() != "church-pc");
    CHECK(store.machine_id().size() == 32);
}

TEST_CASE("MachineStore leaves its settings alone when it cannot write", "[library][machine]")
{
    const seventhslide::test::TempDir tmp;
    // The data dir is a file, so nothing can be written under it.
    const auto blocked = tmp.path / "blocked";
    std::ofstream(blocked) << "x";
    MachineStore store(blocked);
    store.load();
    CHECK(store.machine_id().size() == 32);  // for this run, at least
    CHECK_FALSE(store.save_settings(MachineSettings{}.to_json()));
    CHECK_FALSE(store.settings().has_value());
}
