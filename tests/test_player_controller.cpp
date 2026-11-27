#include <catch2/catch_test_macros.hpp>

#include "theme_test_helpers.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/layout/text_measurer.hpp>
#include <seventhslide/session/player_controller.hpp>

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

using nlohmann::json;
using seventhslide::layout::ApproxTextMeasurer;
using seventhslide::model::OutputConfig;
using seventhslide::build::BuildKind;
using seventhslide::session::OutputNavState;
using seventhslide::session::PlayerController;
using seventhslide::build::SlideBuildInput;
using seventhslide::build::build_slides;
using seventhslide::model::ContentKind;

namespace {

json load_fixture()
{
    const std::string path =
        std::string(SEVENTHSLIDE_FIXTURES_DIR) + "/session/player_controller.expected.json";
    std::ifstream in(path);
    REQUIRE(in.good());
    json j;
    in >> j;
    return j;
}

PlayerController controller_from_maps(const json& maps, int total_lines)
{
    std::vector<OutputNavState> outs;
    for (auto it = maps.begin(); it != maps.end(); ++it) {
        OutputNavState oc;
        oc.output_id = it.key();
        oc.line_to_slide = it->get<std::vector<int>>();
        outs.push_back(std::move(oc));
    }
    PlayerController p;
    std::vector<std::string> lines;
    std::vector<std::string> labels;
    for (int i = 0; i < total_lines; ++i) {
        lines.push_back("L" + std::to_string(i));
        labels.push_back("v" + std::to_string(i));
    }
    p.reset_maps(std::move(outs), total_lines, std::move(lines), std::move(labels));
    return p;
}

void require_frame(const PlayerController& p, const json& fr)
{
    REQUIRE(p.line_cursor() == fr.at("line_cursor").get<int>());
    for (auto it = fr.at("indices").begin(); it != fr.at("indices").end(); ++it) {
        bool found = false;
        for (const auto& oc : p.outputs()) {
            if (oc.output_id == it.key()) {
                REQUIRE(oc.index == it->get<int>());
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

} // namespace

TEST_CASE("PlayerController nav matches the golden fixture", "[session]")
{
    const auto doc = load_fixture();
    REQUIRE(doc.at("schema") == "seventhslide.player_controller.v1");

    for (const auto& c : doc.at("nav_cases")) {
        INFO(c.at("name").get<std::string>());
        auto p = controller_from_maps(c.at("maps"), c.at("total_lines").get<int>());
        const auto& frames = c.at("frames");
        REQUIRE_FALSE(frames.empty());
        require_frame(p, frames.at(0));

        std::size_t fi = 1;
        for (const auto& act : c.at("actions")) {
            INFO(act.dump());
            bool ok = false;
            const auto op = act.at("op").get<std::string>();
            if (op == "next") {
                ok = p.next_slide();
            } else if (op == "prev") {
                ok = p.prev_slide();
            } else if (op == "jump") {
                ok = p.jump_to_line(act.at("line").get<int>());
            } else {
                FAIL("unknown op");
            }
            REQUIRE(fi < frames.size());
            const auto& fr = frames.at(fi++);
            REQUIRE(ok == fr.at("ok").get<bool>());
            require_frame(p, fr);
        }
        REQUIRE(fi == frames.size());
    }
}

TEST_CASE("PlayerController apply_build_result syncs indices and resets the cursor", "[session]")
{
    OutputConfig oc;
    oc.id = "out-main";
    oc.name = "Main";
    seventhslide::test::set_look(oc, {{"width_px", 1000}, {"height_px", 800}});

    SlideBuildInput in;
    in.kind = BuildKind::Song;
    in.selection.kind = ContentKind::Song;
    // Forced breaks -> one line per slide so next_slide has change points.
    in.song.lyrics = "---[Verse:1]---\nOne\n[---][---]\nTwo\n[---][---]\nThree\n";
    in.song.verse_order = "v1";
    in.outputs = {oc};

    ApproxTextMeasurer measurer;
    const auto built = build_slides(in, measurer);
    REQUIRE(built.player.total_lines >= 3);

    PlayerController p;
    p.apply_build_result(built);
    REQUIRE(p.total_lines() == built.player.total_lines);
    REQUIRE(p.line_cursor() == 0);
    REQUIRE(p.outputs().size() == 1);
    REQUIRE(p.outputs()[0].output_id == "out-main");
    REQUIRE(p.outputs()[0].index == p.outputs()[0].line_to_slide[0]);
    REQUIRE(p.find_output("out-main") == &p.outputs()[0]);
    REQUIRE(p.find_output("Main") == nullptr);

    REQUIRE(p.next_slide());
    REQUIRE(p.line_cursor() > 0);

    p.apply_build_result(built);
    REQUIRE(p.line_cursor() == 0);
}

TEST_CASE("slide_for_line is bounds-safe", "[session]")
{
    OutputNavState oc;
    oc.line_to_slide = {2, 3};
    REQUIRE(PlayerController::slide_for_line(oc, 0) == 2);
    REQUIRE(PlayerController::slide_for_line(oc, 1) == 3);
    REQUIRE(PlayerController::slide_for_line(oc, 2) == 0);
    REQUIRE(PlayerController::slide_for_line(oc, -1) == 0);
    OutputNavState empty;
    REQUIRE(PlayerController::slide_for_line(empty, 0) == 0);
}
