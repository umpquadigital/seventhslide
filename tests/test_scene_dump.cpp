#include <catch2/catch_test_macros.hpp>

#include "fixture_files.hpp"

#include <seventhslide/devsupport/scene_dump.hpp>
#include <seventhslide/layout/text_measurer.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::devsupport::native_scene_dump_from_oracle_fixture;
using seventhslide::devsupport::validate_scene_document;

namespace {

using seventhslide::test::oracle_dir;

using seventhslide::test::load_json;

void require_layout_matches(const json& native_out, const json& oracle_out)
{
    REQUIRE(native_out.at("groups") == oracle_out.at("groups"));
    REQUIRE(native_out.at("line_to_slide") == oracle_out.at("line_to_slide"));
    REQUIRE(native_out.at("verse_codes") == oracle_out.at("verse_codes"));
    REQUIRE(native_out.at("verse_indices") == oracle_out.at("verse_indices"));
    REQUIRE(native_out.at("slide_count") == oracle_out.at("slide_count"));
    REQUIRE(native_out.at("all_lines") == oracle_out.at("all_lines"));
    REQUIRE(native_out.at("line_labels") == oracle_out.at("line_labels"));
    REQUIRE(native_out.at("overlay_nonempty") == oracle_out.at("overlay_nonempty"));
    REQUIRE(native_out.at("bg_override_present") == oracle_out.at("bg_override_present"));
}

}  // namespace

TEST_CASE("scene dump matches the song fixtures", "[layout][oracle][scene]")
{
    const auto manifest = load_json(oracle_dir() / "manifest.json");
    ApproxTextMeasurer measurer;

    for (const auto& id_j : manifest.at("fixtures")) {
        const auto id = id_j.get<std::string>();
        INFO(id);
        const auto oracle = load_json(oracle_dir() / (id + ".json"));
        REQUIRE(validate_scene_document(oracle).empty());

        const auto native = native_scene_dump_from_oracle_fixture(oracle, measurer);
        REQUIRE(validate_scene_document(native).empty());
        REQUIRE(native.at("oracle") == "native");
        REQUIRE(native.at("schema") == "seventhslide.scene_dump");
        REQUIRE(native.at("schema_version") == 1);
        REQUIRE(native.at("input").at("lyrics") == oracle.at("input").at("lyrics"));
        REQUIRE(native.at("input").at("verse_order") == oracle.at("input").at("verse_order"));

        REQUIRE(native.at("outputs").size() == 1);
        require_layout_matches(native.at("outputs").at(0), oracle.at("outputs").at(0));

        REQUIRE(native.at("player").at("total_lines") == oracle.at("player").at("total_lines"));
        REQUIRE(native.at("player").at("all_lines") == oracle.at("player").at("all_lines"));
        REQUIRE(
            native.at("player").at("all_line_labels")
            == oracle.at("player").at("all_line_labels"));
    }
}

TEST_CASE("validate_scene_document rejects bad schema", "[layout][oracle]")
{
    json bad = {{"schema", "nope"}, {"schema_version", 1}};
    const auto errs = validate_scene_document(bad);
    REQUIRE_FALSE(errs.empty());
}
