#include <catch2/catch_test_macros.hpp>

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/geom/canvas_presets.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <set>
#include <string>
#include <utility>
#include <vector>

using seventhslide::geom::CanvasShape;
using seventhslide::geom::CanvasSize;
using seventhslide::geom::canvas_aspect_label;
using seventhslide::geom::canvas_preset_title;
using seventhslide::geom::canvas_shape_heading;
using seventhslide::geom::canvas_size_problem;
using seventhslide::geom::find_canvas_preset;
using seventhslide::geom::k_canvas_presets;
using seventhslide::geom::k_max_canvas;

TEST_CASE("Each canvas preset is its own size and can be an output's canvas",
          "[geom][canvas]")
{
    std::vector<seventhslide::model::OutputConfig> outs;
    outs.push_back(seventhslide::theme::new_output("Main", outs));
    std::set<std::pair<int, int>> sizes;
    std::set<std::string> titles;
    for (const auto& preset : k_canvas_presets) {
        INFO(canvas_preset_title(preset));
        CHECK(sizes.emplace(preset.size.width, preset.size.height).second);
        CHECK(titles.insert(canvas_preset_title(preset)).second);
        CHECK(find_canvas_preset(preset.size) == &preset);

        auto general = seventhslide::model::output_general(outs.front());
        general.canvas_width = preset.size.width;
        general.canvas_height = preset.size.height;
        CHECK(seventhslide::model::update_output_settings(outs.front(), outs, 0, general).ok);
    }
}

TEST_CASE("A canvas preset is named with its shape, under its shape's heading",
          "[geom][canvas]")
{
    const auto* hd = find_canvas_preset({1920, 1080});
    REQUIRE(hd != nullptr);
    CHECK(canvas_preset_title(*hd) == "1080p 16:9");
    CHECK(canvas_shape_heading(hd->shape) == "16:9 Widescreen");

    const auto* standard = find_canvas_preset({1440, 1080});
    REQUIRE(standard != nullptr);
    CHECK(canvas_preset_title(*standard) == "1080p 4:3");

    const auto* vertical = find_canvas_preset({1080, 1920});
    REQUIRE(vertical != nullptr);
    CHECK(canvas_preset_title(*vertical) == "1080p Vertical");
    CHECK(canvas_shape_heading(vertical->shape) == "9:16 Vertical");

    CHECK(canvas_shape_heading(CanvasShape::Widescreen16x10) == "16:10 Projector");
    CHECK(find_canvas_preset({1366, 768}) == nullptr);
    CHECK(find_canvas_preset({0, 0}) == nullptr);
}

TEST_CASE("A canvas's shape is named when it is a usual ratio, and a number when not",
          "[geom][canvas]")
{
    CHECK(canvas_aspect_label({1920, 1080}) == "16:9");
    CHECK(canvas_aspect_label({3840, 2160}) == "16:9");
    CHECK(canvas_aspect_label({1080, 1920}) == "9:16");
    CHECK(canvas_aspect_label({1920, 1200}) == "16:10");
    CHECK(canvas_aspect_label({1440, 1080}) == "4:3");
    CHECK(canvas_aspect_label({1280, 1024}) == "5:4");
    CHECK(canvas_aspect_label({1080, 1080}) == "1:1");
    CHECK(canvas_aspect_label({3840, 1080}) == "32:9");
    // Near a usual ratio is not one: say what it is.
    CHECK(canvas_aspect_label({1366, 768}) == "1.78:1");
    CHECK(canvas_aspect_label({2560, 1080}) == "2.37:1");
    CHECK(canvas_aspect_label({608, 1080}) == "0.56:1");
    CHECK(canvas_aspect_label({0, 1080}).empty());
    CHECK(canvas_aspect_label({-4, 3}).empty());
}

TEST_CASE("A canvas size says why it cannot be saved, and saving refuses it with that",
          "[geom][canvas]")
{
    CHECK(canvas_size_problem({1920, 1080}).empty());
    CHECK(canvas_size_problem(k_max_canvas).empty());
    CHECK(canvas_size_problem({1, 1}).empty());
    CHECK(canvas_size_problem({0, 1080}) == "The canvas needs a width and a height");
    CHECK(canvas_size_problem({1920, -1}) == "The canvas needs a width and a height");
    CHECK(canvas_size_problem({7682, 1080}) == "The canvas can be up to 7680 × 4320");
    CHECK(canvas_size_problem({4320, 7680}) == "The canvas can be up to 7680 × 4320");

    std::vector<seventhslide::model::OutputConfig> outs;
    outs.push_back(seventhslide::theme::new_output("Main", outs));
    auto general = seventhslide::model::output_general(outs.front());
    general.canvas_width = 9000;
    const auto refused = seventhslide::model::update_output_settings(outs.front(), outs, 0, general);
    CHECK_FALSE(refused.ok);
    CHECK(refused.message == canvas_size_problem({9000, general.canvas_height}));
    CHECK(outs.front().canvas_width == 1920);
}
