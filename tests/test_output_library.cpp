#include <seventhslide/model/output_library.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

using seventhslide::model::OutputConfig;
using seventhslide::model::remove_output;
using seventhslide::model::reorder_outputs;
using seventhslide::model::sanitize_output_name;
using seventhslide::model::unique_output_name;
using seventhslide::model::update_output_settings;

namespace {

/// Append a new output the way the app does, and return a copy of it.
OutputConfig add_output(std::vector<OutputConfig>& outs, std::string_view name)
{
    outs.push_back(seventhslide::theme::new_output(name, outs));
    return outs.back();
}

}  // namespace

TEST_CASE("output_library sanitizes and uniquifies names", "[model]")
{
    REQUIRE(sanitize_output_name("  Main/Out:1  ") == "MainOut1");
    REQUIRE(sanitize_output_name("...") == "Output");
    REQUIRE(sanitize_output_name("") == "Output");

    std::vector<OutputConfig> outs;
    outs.push_back(OutputConfig{});
    outs.back().name = "Main";
    REQUIRE(unique_output_name("Main", outs) == "Main 2");
    REQUIRE(unique_output_name("main", outs) == "main 2");
    REQUIRE(unique_output_name("Main", outs, 0) == "Main");
}

TEST_CASE("unique_output_name numbers a long name from its 60-character cut", "[model]")
{
    std::vector<OutputConfig> outs(1);
    // A free name is kept whole, up to the 64 the sanitiser allows.
    const std::string long_name(64, 'a');
    REQUIRE(unique_output_name(long_name, outs) == long_name);

    // Taken, it is cut to 60 before the number goes on.
    outs[0].name = long_name;
    REQUIRE(unique_output_name(long_name, outs) == std::string(60, 'a') + " 2");

    // The cut drops the spaces and dots it leaves at the end.
    const std::string dotted = std::string(58, 'b') + ". cccc";
    REQUIRE(sanitize_output_name(dotted) == dotted);
    outs[0].name = dotted;
    REQUIRE(unique_output_name(dotted, outs) == std::string(58, 'b') + " 2");

    // Numbering skips taken numbers, ignoring case.
    outs[0].name = "Main";
    outs.push_back(OutputConfig{});
    outs.back().name = "main 2";
    REQUIRE(unique_output_name("MAIN", outs) == "MAIN 3");
}

TEST_CASE("output_library add/update/remove/reorder", "[model]")
{
    std::vector<OutputConfig> outs;
    add_output(outs, "Main");
    REQUIRE(outs.size() == 1);
    REQUIRE_FALSE(outs[0].song_themes.empty());
    REQUIRE_FALSE(outs[0].bg_themes.empty());

    REQUIRE(add_output(outs, "Main").name == "Main 2");
    REQUIRE(outs.size() == 2);

    auto general = seventhslide::model::output_general(outs[1]);
    general.name = "Foldback";
    general.canvas_width = 1280;
    general.canvas_height = 720;
    general.enabled_stack_ids = {"main"};
    general.disabled_content_types = {"video", "image"};
    general.exempt_from_global_blank = true;
    general.icon = "pulpit";
    auto upd = update_output_settings(outs[1], outs, 1, general);
    REQUIRE(upd.ok);
    REQUIRE(outs[1].name == "Foldback");
    REQUIRE(outs[1].canvas_width == 1280);
    REQUIRE(outs[1].enabled_stack_ids == std::vector<std::string>{"main"});
    REQUIRE(outs[1].disabled_content_types == std::vector<std::string>{"video", "image"});
    REQUIRE(outs[1].exempt_from_global_blank == true);
    REQUIRE(outs[1].icon == "pulpit");
    // Themes preserved
    REQUIRE_FALSE(outs[1].song_themes.empty());

    REQUIRE(reorder_outputs(outs, {"Foldback", "Main"}).ok);
    REQUIRE(outs[0].name == "Foldback");
    REQUIRE(outs[1].name == "Main");

    REQUIRE(remove_output(outs, 0).ok);
    REQUIRE(outs.size() == 1);
    // The last one too: a library may have no outputs.
    REQUIRE(remove_output(outs, 0).ok);
    REQUIRE(outs.empty());
    REQUIRE_FALSE(remove_output(outs, 0).ok);
}

TEST_CASE("every output gets a stable id that no other output shares", "[model][outputs]")
{
    std::vector<OutputConfig> outs(3);
    outs[0].id = "keep";
    outs[1].id = "keep";  // duplicate: the later one is re-issued
    // outs[2] has none

    REQUIRE(seventhslide::model::ensure_output_ids(outs));
    REQUIRE(outs[0].id == "keep");
    REQUIRE_FALSE(outs[1].id.empty());
    REQUIRE_FALSE(outs[2].id.empty());
    REQUIRE(outs[1].id != "keep");
    REQUIRE(outs[1].id != outs[2].id);

    // Already valid: nothing to do.
    REQUIRE_FALSE(seventhslide::model::ensure_output_ids(outs));
    REQUIRE(seventhslide::model::find_output_by_id(outs, outs[2].id) == &outs[2]);
    REQUIRE(seventhslide::model::find_output_by_id(outs, "") == nullptr);
}

TEST_CASE("the same outputs put right twice get the same ids", "[model][outputs]")
{
    // A library this computer only views is put right on every open and
    // never written.
    std::vector<OutputConfig> outs(3);
    outs[0].id = "keep";
    outs[1].id = "keep";
    auto again = outs;
    REQUIRE(seventhslide::model::ensure_output_ids(outs));
    REQUIRE(seventhslide::model::ensure_output_ids(again));
    for (std::size_t i = 0; i < outs.size(); ++i) {
        CHECK(outs[i].id == again[i].id);
    }
}

TEST_CASE("renaming an output keeps its id, and the id is persisted", "[model][outputs]")
{
    std::vector<OutputConfig> outs;
    const auto added = add_output(outs, "Main");
    REQUIRE_FALSE(added.id.empty());

    auto general = seventhslide::model::output_general(outs[0]);
    general.name = "Stage";
    const auto renamed = seventhslide::model::update_output_settings(outs[0], outs, 0, general);
    REQUIRE(renamed.ok);
    REQUIRE(outs[0].name == "Stage");
    REQUIRE(outs[0].id == added.id);

    const auto reloaded = OutputConfig::from_dict(outs[0].to_persist_dict());
    REQUIRE(reloaded.id == added.id);
    REQUIRE(reloaded.name == "Stage");
}

namespace {

[[nodiscard]] std::vector<std::string> output_names(const std::vector<OutputConfig>& outs)
{
    std::vector<std::string> names;
    for (const auto& oc : outs) {
        names.push_back(oc.name);
    }
    return names;
}

}  // namespace

TEST_CASE("a rejected output reorder leaves the list untouched", "[model][outputs]")
{
    std::vector<OutputConfig> outs;
    add_output(outs, "Main");
    add_output(outs, "Stage");
    add_output(outs, "Foldback");
    const auto before = output_names(outs);

    SECTION("an unknown name after a known one")
    {
        const auto r = reorder_outputs(outs, {"Stage", "Typo", "Main"});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Unknown output name");
    }
    SECTION("a repeated name")
    {
        const auto r = reorder_outputs(outs, {"Stage", "stage", "Main"});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Duplicate name in order list");
    }
    SECTION("too few names")
    {
        const auto r = reorder_outputs(outs, {"Stage", "Main"});
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.message == "Name list must include every output");
    }

    REQUIRE(output_names(outs) == before);
    REQUIRE_FALSE(outs[0].song_themes.empty());
}

TEST_CASE("output reorder matches names without regard to case", "[model][outputs]")
{
    std::vector<OutputConfig> outs;
    add_output(outs, "Main");
    add_output(outs, "Stage");
    REQUIRE(reorder_outputs(outs, {"STAGE", "main"}).ok);
    REQUIRE(output_names(outs) == std::vector<std::string>{"Stage", "Main"});
}

TEST_CASE("an output's icon round-trips, and an unknown one is the default", "[model][outputs]")
{
    std::vector<OutputConfig> outs;
    add_output(outs, "Stage");
    REQUIRE(outs[0].icon == seventhslide::model::k_default_output_icon);

    auto general = seventhslide::model::output_general(outs[0]);
    general.icon = "projector";
    REQUIRE(update_output_settings(outs[0], outs, 0, general).ok);
    REQUIRE(OutputConfig::from_dict(outs[0].to_persist_dict()).icon == "projector");

    general.icon = "not-an-icon";
    REQUIRE(update_output_settings(outs[0], outs, 0, general).ok);
    REQUIRE(outs[0].icon == seventhslide::model::k_default_output_icon);

    auto stored = outs[0].to_persist_dict();
    stored["icon"] = "bogus";
    REQUIRE(OutputConfig::from_dict(stored).icon == seventhslide::model::k_default_output_icon);
    stored.erase("icon");
    REQUIRE(OutputConfig::from_dict(stored).icon == seventhslide::model::k_default_output_icon);
}
