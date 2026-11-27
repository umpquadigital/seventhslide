#include <catch2/catch_test_macros.hpp>

#include <seventhslide/library/console_store.hpp>

#include <nlohmann/json.hpp>

#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <fstream>

using seventhslide::library::ConsoleStore;
using seventhslide::library::StackIds;

TEST_CASE("ConsoleStore keeps each library's Preview stacks apart", "[library][console]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const ConsoleStore store(tmp.path().toStdString());
    REQUIRE(store.preview_stacks("default").empty());  // no file yet
    REQUIRE_FALSE(std::filesystem::exists(store.file()));

    REQUIRE(store.set_stack_preview("default", "main", true));
    REQUIRE(store.set_stack_preview("default", "lower", true));
    REQUIRE(store.set_stack_preview("kids", "main", true));
    CHECK(store.preview_stacks("default") == StackIds{"lower", "main"});
    CHECK(store.preview_stacks("kids") == StackIds{"main"});

    // Off again; set twice is the same as once.
    REQUIRE(store.set_stack_preview("default", "lower", false));
    REQUIRE(store.set_stack_preview("default", "main", true));
    CHECK(store.preview_stacks("default") == StackIds{"main"});

    REQUIRE(store.remove_library("kids"));
    CHECK(store.preview_stacks("kids").empty());
    CHECK(store.preview_stacks("default") == StackIds{"main"});
    REQUIRE(store.remove_library("never-existed"));
}

TEST_CASE("ConsoleStore forgets the stacks a library no longer has", "[library][console]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const ConsoleStore store(tmp.path().toStdString());
    REQUIRE(store.set_stack_preview("default", "main", true));
    REQUIRE(store.set_stack_preview("default", "gone", true));
    REQUIRE(store.set_stack_preview("kids", "gone", true));

    REQUIRE(store.keep_only_stacks("default", {"main", "lower"}));
    CHECK(store.preview_stacks("default") == StackIds{"main"});
    // Another library's stack of the same id is its own.
    CHECK(store.preview_stacks("kids") == StackIds{"gone"});

    // Nothing to forget writes nothing.
    const auto written = std::filesystem::last_write_time(store.file());
    std::filesystem::last_write_time(store.file(), written - std::chrono::hours(1));
    REQUIRE(store.keep_only_stacks("default", {"main"}));
    CHECK(std::filesystem::last_write_time(store.file()) == written - std::chrono::hours(1));

    // The last one gone, the library leaves the file.
    REQUIRE(store.keep_only_stacks("default", {}));
    std::ifstream in(store.file());
    const auto doc = nlohmann::json::parse(in);
    CHECK(doc.at("libraries").size() == 1);
    CHECK(doc.at("libraries").contains("kids"));
}

TEST_CASE("ConsoleStore reads a damaged file as empty", "[library][console]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const ConsoleStore store(tmp.path().toStdString());
    {
        std::ofstream out(store.file());
        out << R"({"version": 1, "libraries": {"default": {"preview_stacks": ["main", 7, ""]},)"
            << R"( "kids": {"preview_stacks": "main"}, "odd": 3}})";
    }
    // What is a stack id is read, and nothing else.
    CHECK(store.preview_stacks("default") == StackIds{"main"});
    CHECK(store.preview_stacks("kids").empty());
    CHECK(store.preview_stacks("odd").empty());
    {
        std::ofstream out(store.file());
        out << "{ not json";
    }
    CHECK(store.preview_stacks("default").empty());
    // And the next save replaces it with a good one.
    REQUIRE(store.set_stack_preview("default", "main", true));
    CHECK(store.preview_stacks("default") == StackIds{"main"});
}
