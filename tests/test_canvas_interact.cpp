#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/canvas/interact.hpp>
#include <seventhslide/layout/editor_html.hpp>
#include <seventhslide/model/rich_text.hpp>

#include <string>
#include <utility>
#include <vector>

using Catch::Approx;
using seventhslide::canvas::AxisTargets;
using seventhslide::canvas::Handle;
using seventhslide::canvas::Limits;
using seventhslide::canvas::Rect;
using seventhslide::canvas::clamp_rect;
using seventhslide::canvas::geometry_from_drag;
using seventhslide::canvas::hit_handle;
using seventhslide::canvas::hit_object;
using seventhslide::canvas::k_min_height;
using seventhslide::canvas::k_min_width;
using seventhslide::canvas::nearest_snap;
using seventhslide::canvas::point_in_rect;
using seventhslide::canvas::snap_axis_targets;
using seventhslide::canvas::snap_rect;
using seventhslide::geom::CanvasSize;
using seventhslide::model::RichLine;
using seventhslide::model::SpanAttrs;
using seventhslide::model::TextSpan;
using seventhslide::model::TextStyle;
using seventhslide::model::apply_attrs_to_selection;
using seventhslide::model::format_editor_object;
using seventhslide::model::parse_editor_lines;
using seventhslide::model::plain_text;
using seventhslide::model::style_at_offset;

TEST_CASE("handle_from_string round-trips the stage names", "[canvas][interact]")
{
    REQUIRE(seventhslide::canvas::handle_from_string("nw") == Handle::NW);
    REQUIRE(seventhslide::canvas::handle_from_string("pending") == Handle::Pending);
    REQUIRE(seventhslide::canvas::to_string(Handle::SE) == "se");
    REQUIRE(seventhslide::canvas::handle_from_string("nope") == Handle::None);
}

TEST_CASE("hit_handle uses stage pixels of a canvas rect", "[canvas][interact]")
{
    const Rect box{"a", 100, 50, 200, 80};
    const double scale = 0.5;
    // Stage: x=50, y=25, w=100, h=40. N is the top-centre handle.
    REQUIRE(hit_handle(50, 25, box, scale, 10) == Handle::NW);
    REQUIRE(hit_handle(100, 25, box, scale, 10) == Handle::N);
    REQUIRE(hit_handle(150, 25, box, scale, 10) == Handle::NE);
    REQUIRE(hit_handle(0, 0, box, scale, 10) == Handle::None);
}

TEST_CASE("hit_object walks front to back", "[canvas][interact]")
{
    const std::vector<Rect> objects{
        {"back", 0, 0, 200, 200},
        {"front", 50, 50, 40, 40},
    };
    REQUIRE(hit_object(60, 60, objects)->id == "front");
    REQUIRE(hit_object(10, 10, objects)->id == "back");
    REQUIRE(hit_object(400, 400, objects) == nullptr);
}

TEST_CASE("clamp_rect keeps a sliver on canvas", "[canvas][interact]")
{
    const CanvasSize canvas{1920, 1080};
    const auto out = clamp_rect(Rect{"", -500, 10, 40, 40}, canvas);
    REQUIRE(out.x + out.w == Approx(20));
    REQUIRE(out.w == Approx(k_min_width));
}

TEST_CASE("geometry_from_drag move and west-resize match the JS editor", "[canvas][interact]")
{
    const CanvasSize canvas{1920, 1080};
    const Rect start{"box", 100, 80, 200, 100};
    const auto moved = geometry_from_drag(Handle::Move, start, 15, -10, canvas);
    REQUIRE(moved.x == Approx(115));
    REQUIRE(moved.y == Approx(70));
    REQUIRE(moved.w == Approx(200));

    const auto west = geometry_from_drag(Handle::W, start, 30, 0, canvas);
    REQUIRE(west.x == Approx(130));
    REQUIRE(west.w == Approx(170));
}

TEST_CASE("snap_rect move snaps left edge to a sibling", "[canvas][interact]")
{
    const CanvasSize canvas{1920, 1080};
    const std::vector<Rect> objects{{"a", 40, 10, 80, 40}, {"b", 200, 10, 80, 40}};
    const auto targets = snap_axis_targets(canvas, objects, {"b"});
    Rect moving{"b", 45, 10, 80, 40};
    const auto snapped = snap_rect(moving, Handle::Move, targets, canvas, true);
    REQUIRE(snapped.v_guide == Approx(40));
    REQUIRE(snapped.rect.x == Approx(40));
}

TEST_CASE("snap_axis_targets lists the canvas edges and centre first", "[canvas][interact]")
{
    const CanvasSize canvas{1920, 1080};
    const std::vector<Rect> objects{{"a", 100, 50, 200, 40}, {"skip", 7, 7, 10, 10}};
    const auto targets = snap_axis_targets(canvas, objects, {"skip"});
    CHECK(targets.xs == std::vector<double>{0, 960, 1920, 100, 200, 300});
    CHECK(targets.ys == std::vector<double>{0, 540, 1080, 50, 70, 90});

    // An unset canvas falls back to the reference size, as clamp_rect does.
    const auto fallback = snap_axis_targets(CanvasSize{0, 0}, {}, {});
    CHECK(fallback.xs == std::vector<double>{0, 960, 1920});
    CHECK(fallback.ys == std::vector<double>{0, 540, 1080});
}

TEST_CASE("snap_rect meets the canvas edges", "[canvas][interact]")
{
    const CanvasSize canvas{1920, 1080};
    const auto targets = snap_axis_targets(canvas, {}, {});
    struct Expect {
        Rect rect;
        Handle handle;
        double x, y, w, h, v_guide, h_guide;
    };
    const Expect cases[] = {
        // Moving: the left and top edges meet 0, the right and bottom the far
        // sides, and the size holds.
        {{"b", 6, -4, 200, 100}, Handle::Move, 0, 0, 200, 100, 0, 0},
        {{"b", 1725, 984, 200, 100}, Handle::Move, 1720, 980, 200, 100, 1920, 1080},
        // Resizing: the dragged edge meets the canvas side and the other holds.
        {{"b", 100, 100, 1814, 100}, Handle::E, 100, 100, 1820, 100, 1920, -1},
        {{"b", 7, 100, 293, 100}, Handle::W, 0, 100, 300, 100, 0, -1},
        {{"b", 100, -6, 200, 306}, Handle::N, 100, 0, 200, 300, -1, 0},
        {{"b", 100, 500, 200, 572}, Handle::S, 100, 500, 200, 580, -1, 1080},
    };
    for (const auto& c : cases) {
        INFO(to_string(c.handle) << " from x " << c.rect.x << " y " << c.rect.y);
        const auto s = snap_rect(c.rect, c.handle, targets, canvas, true);
        CHECK(s.rect.x == Approx(c.x));
        CHECK(s.rect.y == Approx(c.y));
        CHECK(s.rect.w == Approx(c.w));
        CHECK(s.rect.h == Approx(c.h));
        CHECK(s.v_guide == Approx(c.v_guide));
        CHECK(s.h_guide == Approx(c.h_guide));
    }

    // A sibling closer than the canvas edge still wins; at an equal distance
    // the canvas edge does.
    const std::vector<Rect> objects{{"a", 8, 400, 80, 40}};
    const auto with_sibling = snap_axis_targets(canvas, objects, {});
    const auto nearer =
        snap_rect(Rect{"b", 6, 200, 80, 40}, Handle::Move, with_sibling, canvas, true);
    CHECK(nearer.v_guide == Approx(8));
    CHECK(nearer.rect.x == Approx(8));
    const auto tie = snap_rect(Rect{"b", 4, 200, 80, 40}, Handle::Move, with_sibling, canvas, true);
    CHECK(tie.v_guide == Approx(0));
    CHECK(tie.rect.x == Approx(0));
}

TEST_CASE("snap_rect moves only the edges a handle holds", "[canvas][interact]")
{
    // Edges: left 100, right 300, top 100, bottom 200. Every guide is within
    // the snap distance of one edge.
    const CanvasSize canvas{1920, 1080};
    const Rect box{"b", 100, 100, 200, 100};
    const AxisTargets targets{{97, 304}, {96, 203}};
    struct Expect {
        Handle handle;
        double x, y, w, h, v_guide, h_guide;
    };
    const Expect cases[] = {
        {Handle::N, 100, 96, 200, 104, -1, 96},
        {Handle::S, 100, 100, 200, 103, -1, 203},
        {Handle::E, 100, 100, 204, 100, 304, -1},
        {Handle::W, 97, 100, 203, 100, 97, -1},
        {Handle::NE, 100, 96, 204, 104, 304, 96},
        {Handle::NW, 97, 96, 203, 104, 97, 96},
        {Handle::SE, 100, 100, 204, 103, 304, 203},
        {Handle::SW, 97, 100, 203, 103, 97, 203},
        // Moving keeps the size; on each axis the closest edge wins.
        {Handle::Move, 97, 103, 200, 100, 97, 203},
        {Handle::Pending, 97, 103, 200, 100, 97, 203},
    };
    for (const auto& c : cases) {
        INFO(to_string(c.handle));
        const auto s = snap_rect(box, c.handle, targets, canvas, true);
        CHECK(s.rect.x == Approx(c.x));
        CHECK(s.rect.y == Approx(c.y));
        CHECK(s.rect.w == Approx(c.w));
        CHECK(s.rect.h == Approx(c.h));
        CHECK(s.v_guide == Approx(c.v_guide));
        CHECK(s.h_guide == Approx(c.h_guide));
    }

    // The centre snaps too, and on a tie the earlier edge (start, centre,
    // end) wins.
    const auto centred = snap_rect(box, Handle::Move, AxisTargets{{198}, {}}, canvas, true);
    CHECK(centred.rect.x == Approx(98));
    CHECK(centred.v_guide == Approx(198));
    const auto tie = snap_rect(box, Handle::Move, AxisTargets{{95, 305}, {}}, canvas, true);
    CHECK(tie.rect.x == Approx(95));
    // A resize still keeps its minimum size.
    // Right edge 115 snaps to 108, which would leave 8.
    const Rect thin{"t", 100, 100, 15, 30};
    const auto pinched = snap_rect(thin, Handle::E, AxisTargets{{108}, {}}, canvas, true,
                                   Limits{.min_w = 20});
    CHECK(pinched.v_guide == Approx(108));
    CHECK(pinched.rect.w == Approx(20));
}

TEST_CASE("nearest_snap prefers the first equally close target", "[canvas][interact]")
{
    REQUIRE(*nearest_snap(10, {0, 10, 20}, 5) == Approx(10));
    REQUIRE_FALSE(nearest_snap(10, {0, 20}, 5).has_value());
}

TEST_CASE("apply_attrs_to_selection splits and restyles the middle run", "[model][rich_text][canvas]")
{
    const std::vector<RichLine> lines{RichLine{TextSpan{"Hello world", TextStyle{}}}};
    SpanAttrs attrs;
    attrs.bold = true;
    const auto out = apply_attrs_to_selection(lines, 6, 11, attrs).front();
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].text == "Hello ");
    REQUIRE_FALSE(out[0].style.bold);
    REQUIRE(out[1].text == "world");
    REQUIRE(out[1].style.bold);
}

TEST_CASE("apply_attrs_to_selection merges runs that end up alike", "[model][rich_text][canvas]")
{
    const TextStyle bold{.bold = true};
    const std::vector<RichLine> lines{
        RichLine{TextSpan{"Hello ", bold}, TextSpan{"big ", {}}, TextSpan{"world", bold}}};
    SpanAttrs attrs;
    attrs.bold = true;

    // Bolding the plain middle leaves one bold run, not three.
    const auto merged = apply_attrs_to_selection(lines, 6, 10, attrs);
    REQUIRE(merged == std::vector<RichLine>{RichLine{TextSpan{"Hello big world", bold}}});

    // A range across runs restyles only its own bytes.
    attrs.bold = false;
    const auto cut = apply_attrs_to_selection(merged, 3, 12, attrs);
    REQUIRE(cut
            == std::vector<RichLine>{
                RichLine{TextSpan{"Hel", bold}, TextSpan{"lo big wo", {}}, TextSpan{"rld", bold}}});
}

TEST_CASE("apply_attrs_to_selection sets only a family and colour the markup reads back",
          "[model][rich_text][canvas]")
{
    const std::vector<RichLine> lines{RichLine{TextSpan{"Hello", TextStyle{}}}};
    const auto styled = [&](const SpanAttrs& attrs) {
        const auto out = apply_attrs_to_selection(lines, 0, 5, attrs);
        // What is set survives being stored: no tag comes back as text.
        const auto stored = seventhslide::model::format_markup_lines(out);
        const auto read = seventhslide::model::parse_markup_lines(stored);
        REQUIRE(read.size() == 1);
        REQUIRE(plain_text(read.front()) == "Hello");
        REQUIRE(read.front().front().style.font_family == out.front().front().style.font_family);
        return out.front().front().style;
    };
    const auto family = [&](std::string name) {
        SpanAttrs attrs;
        attrs.font_family = std::move(name);
        return styled(attrs).font_family;
    };
    const auto colour = [&](std::string value) {
        SpanAttrs attrs;
        attrs.color = std::move(value);
        return styled(attrs).color;
    };

    REQUIRE(family("Open Sans Light") == "Open Sans Light");
    REQUIRE(family(std::string(64, 'a')) == std::string(64, 'a'));
    REQUIRE(family("The \"Best\" Sans") == "The \"Best\" Sans");
    REQUIRE(family(std::string(65, 'a')).empty());
    REQUIRE(family("A>B").empty());
    REQUIRE(family("A&B").empty());
    REQUIRE(family("A<B").empty());
    REQUIRE(family("\"Quoted\"").empty());
    REQUIRE(family(" Padded").empty());

    REQUIRE(colour("#FF8800") == "#FF8800");
    REQUIRE(colour("#80ff8800") == "#80ff8800");
    REQUIRE(colour("ff8800") == "ff8800");
    REQUIRE(colour("#ff88").size() == 5);
    REQUIRE(colour("#12345").empty());
    REQUIRE(colour("red").empty());
    REQUIRE(colour("#ff8800>").empty());

    // A refused value leaves the one already set.
    SpanAttrs both;
    both.font_family = "Open Sans";
    both.color = "#112233";
    const auto set = apply_attrs_to_selection(lines, 0, 5, both);
    SpanAttrs bad;
    bad.font_family = std::string(65, 'b');
    bad.color = "#1234567";
    const auto kept = apply_attrs_to_selection(set, 0, 5, bad);
    REQUIRE(kept == set);
}

TEST_CASE("apply_attrs_to_selection crosses a newline", "[model][rich_text][canvas]")
{
    std::vector<RichLine> lines{{TextSpan{"AB", {}}}, {TextSpan{"CD", {}}}};
    SpanAttrs attrs;
    attrs.italic = true;
    // "AB\nCD" - select from B through C (bytes 1..4)
    const auto out = apply_attrs_to_selection(lines, 1, 4, attrs);
    REQUIRE(plain_text(out[0]) == "AB");
    REQUIRE_FALSE(out[0][0].style.italic);
    REQUIRE(out[0][1].style.italic);
    REQUIRE(out[0][1].text == "B");
    REQUIRE(out[1][0].style.italic);
    REQUIRE(out[1][0].text == "C");
    REQUIRE_FALSE(out[1][1].style.italic);
}

TEST_CASE("parse_editor_lines applies box flags only when markup is plain", "[model][rich_text][canvas]")
{
    const auto plain = parse_editor_lines("Hi", true, false, false);
    REQUIRE(plain[0][0].style.bold);
    const auto marked = parse_editor_lines("<i>Hi</i>", true, false, false);
    REQUIRE(marked[0][0].style.italic);
    REQUIRE_FALSE(marked[0][0].style.bold);
}

TEST_CASE("format_editor_object keeps uniform flags off the markup", "[model][rich_text][canvas]")
{
    TextStyle bold;
    bold.bold = true;
    const auto uniform = format_editor_object({{TextSpan{"Hi", bold}}});
    REQUIRE(uniform.text == "Hi");
    REQUIRE(uniform.bold);
    REQUIRE_FALSE(uniform.mixed);

    TextStyle sized = bold;
    sized.size_pct = 80;
    const auto mixed = format_editor_object({{TextSpan{"Hi", sized}}});
    REQUIRE(mixed.mixed);
    REQUIRE_FALSE(mixed.bold);
    REQUIRE(mixed.text.find("<size=80>") != std::string::npos);
}

TEST_CASE("style_at_offset reads the run under the caret", "[model][rich_text][canvas]")
{
    TextStyle red;
    red.color = "#ff0000";
    const std::vector<RichLine> lines{{TextSpan{"Hi", {}}, TextSpan{" there", red}}};
    REQUIRE(style_at_offset(lines, 0).color.empty());
    REQUIRE(style_at_offset(lines, 3).color == "#ff0000");
}

TEST_CASE("editor_html keeps blank lines as Qt empty paragraphs", "[layout][editor_html]")
{
    const std::vector<RichLine> lines{{TextSpan{"Hi", {}}}, {TextSpan{"", {}}}};
    seventhslide::layout::EditorHtmlOptions opts;
    opts.font_size = 48;
    opts.stage_scale = 1;
    const auto html = seventhslide::layout::editor_html(lines, opts);
    REQUIRE(html.find("-qt-paragraph-type:empty") != std::string::npos);
    REQUIRE(html.find("Hi") != std::string::npos);
    REQUIRE(html.find("white-space:pre-wrap") != std::string::npos);
}

TEST_CASE("editor_html writes a family and colour as entities inside its attributes",
          "[layout][editor_html]")
{
    TextStyle style;
    style.font_family = "Bad\" onclick=\"x<b>&";
    style.color = "#ff0000\"><b";
    const std::vector<RichLine> lines{{TextSpan{"Hi", style}}, {TextSpan{"", style}}};
    seventhslide::layout::EditorHtmlOptions opts;
    const auto html = seventhslide::layout::editor_html(lines, opts);
    REQUIRE(html.find("Bad&quot; onclick=&quot;x&lt;b&gt;&amp;") != std::string::npos);
    REQUIRE(html.find("#ff0000&quot;&gt;&lt;b") != std::string::npos);
    REQUIRE(html.find("onclick=\"") == std::string::npos);
    REQUIRE(html.find("x<b>") == std::string::npos);
    REQUIRE(html.find("\"><b;") == std::string::npos);
}

TEST_CASE("point_in_rect is inclusive on the far edge", "[canvas][interact]")
{
    const Rect r{"", 10, 10, 20, 20};
    REQUIRE(point_in_rect(10, 10, r));
    REQUIRE(point_in_rect(30, 30, r));
    REQUIRE_FALSE(point_in_rect(30.1, 20, r));
}
