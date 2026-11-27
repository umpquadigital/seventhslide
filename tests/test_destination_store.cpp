#include <catch2/catch_test_macros.hpp>

#include <seventhslide/library/destination_store.hpp>

#include <nlohmann/json.hpp>

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using seventhslide::library::DestinationStore;
using nlohmann::json;

namespace {

[[nodiscard]] json record(const char* name)
{
    return json{{"id", name}, {"type", "display"}, {"name", name}};
}

}  // namespace

TEST_CASE("DestinationStore keeps libraries and outputs apart", "[library][destinations]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const DestinationStore store(tmp.path().toStdString());

    REQUIRE(store.load("default").empty());  // no file yet

    REQUIRE(store.save("default", {{"out-main", json::array({record("Sanctuary")})},
                                   {"out-stage", json::array({record("Confidence")})}}));
    REQUIRE(store.save("kids", {{"out-main", json::array({record("Kids Room")})}}));

    auto main_lib = store.load("default");
    REQUIRE(main_lib.size() == 2);
    REQUIRE(main_lib.at("out-main").at(0).at("name") == "Sanctuary");
    REQUIRE(store.load("kids").at("out-main").at(0).at("name") == "Kids Room");

    // Saving names only the outputs it covers; an empty array clears one.
    REQUIRE(store.save("default", {{"out-stage", json::array()}}));
    main_lib = store.load("default");
    REQUIRE(main_lib.size() == 1);
    REQUIRE(main_lib.count("out-main") == 1);

    REQUIRE(store.remove_output("default", "out-main"));
    REQUIRE(store.load("default").empty());
    REQUIRE(store.remove_library("kids"));
    REQUIRE(store.load("kids").empty());
    REQUIRE(store.remove_library("never-existed"));
}

TEST_CASE("DestinationStore reads a damaged file as empty", "[library][destinations]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const DestinationStore store(tmp.path().toStdString());
    {
        std::ofstream out(store.file());
        out << "{ not json";
    }
    REQUIRE(store.load("default").empty());
    // And the next save replaces it with a good one.
    REQUIRE(store.save("default", {{"out-main", json::array({record("Sanctuary")})}}));
    REQUIRE(store.load("default").size() == 1);
}

TEST_CASE("DestinationStore keeps which outputs are off, per library", "[library][destinations]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const DestinationStore store(tmp.path().toStdString());
    REQUIRE(store.outputs_off("default").empty());  // no file yet: all on

    REQUIRE(store.save("default", {{"out-stage", json::array({record("Confidence")})}}));
    REQUIRE(store.set_output_on("default", "out-stage", false));
    REQUIRE(store.set_output_on("default", "out-stream", false));
    REQUIRE(store.set_output_on("kids", "out-main", false));
    CHECK(store.outputs_off("default") == std::set<std::string, std::less<>>{"out-stage", "out-stream"});
    CHECK(store.outputs_off("kids").size() == 1);
    // Switching one off keeps where it is shown, for when it is back on.
    CHECK(store.load("default").at("out-stage").at(0).at("name") == "Confidence");

    REQUIRE(store.set_output_on("default", "out-stream", true));
    CHECK(store.outputs_off("default") == std::set<std::string, std::less<>>{"out-stage"});
    // Records saved after keep it off.
    REQUIRE(store.save("default", {{"out-stage", json::array()}}));
    CHECK(store.outputs_off("default").size() == 1);

    // A deleted output, or library, is forgotten.
    REQUIRE(store.remove_output("default", "out-stage"));
    CHECK(store.outputs_off("default").empty());
    REQUIRE(store.remove_library("kids"));
    CHECK(store.outputs_off("kids").empty());
}
