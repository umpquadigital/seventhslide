#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <seventhslide/canvas/interact.hpp>
#include <seventhslide/canvas/layout.hpp>

#include <utility>
#include <vector>

using Catch::Approx;
using seventhslide::canvas::Align;
using seventhslide::canvas::Distribute;
using seventhslide::canvas::FlipAxis;
using seventhslide::canvas::Handle;
using seventhslide::canvas::Rect;
using seventhslide::canvas::aabb;
using seventhslide::canvas::align_rects;
using seventhslide::canvas::distribute_rects;
using seventhslide::canvas::expand_groups;
using seventhslide::canvas::flip_rects;
using seventhslide::canvas::geometry_from_drag_many;
using seventhslide::canvas::hit_marquee;
using seventhslide::canvas::normalized_rect;
using seventhslide::canvas::nudge_rects;
using seventhslide::canvas::point_in_rect;
using seventhslide::canvas::rects_intersect;
using seventhslide::canvas::snap_axis_targets;
using seventhslide::canvas::snap_many;
using seventhslide::canvas::union_bounds;
using seventhslide::geom::CanvasSize;

TEST_CASE("normalized_rect flips negative extents", "[canvas][layout]")
{
    const auto box = normalized_rect(Rect{"", 100, 80, -40, -20});
    REQUIRE(box.x == Approx(60));
    REQUIRE(box.y == Approx(60));
    REQUIRE(box.w == Approx(40));
    REQUIRE(box.h == Approx(20));
}

TEST_CASE("aabb of a 90-degree box is the swapped extents", "[canvas][layout]")
{
    const Rect box{"r", 100, 40, 80, 20, 90};
    const auto bounds = aabb(box);
    REQUIRE(bounds.rotation == Approx(0));
    REQUIRE(bounds.w == Approx(20));
    REQUIRE(bounds.h == Approx(80));
    REQUIRE(bounds.x + bounds.w * 0.5 == Approx(140));
    REQUIRE(bounds.y + bounds.h * 0.5 == Approx(50));
}

TEST_CASE("union_bounds covers every member", "[canvas][layout]")
{
    const auto box = union_bounds({{"a", 10, 20, 30, 40}, {"b", 50, 5, 10, 10}});
    REQUIRE(box.x == Approx(10));
    REQUIRE(box.y == Approx(5));
    REQUIRE(box.w == Approx(50));
    REQUIRE(box.h == Approx(55));
}

TEST_CASE("hit_marquee returns front-most first", "[canvas][layout]")
{
    const std::vector<Rect> objects{{"back", 0, 0, 100, 100}, {"front", 40, 40, 20, 20}};
    const auto ids = hit_marquee(Rect{"", 30, 30, 30, 30}, objects);
    REQUIRE(ids == std::vector<std::string>{"front", "back"});
    REQUIRE(hit_marquee(Rect{"", 200, 200, 10, 10}, objects).empty());
}

TEST_CASE("expand_groups pulls every member of a touched group", "[canvas][layout]")
{
    const std::vector<std::pair<std::string, std::string>> members{
        {"a", "g1"}, {"b", "g1"}, {"c", ""}, {"d", "g2"}};
    REQUIRE(expand_groups(members, {"a"}) == std::vector<std::string>{"a", "b"});
    REQUIRE(expand_groups(members, {"c"}) == std::vector<std::string>{"c"});
    REQUIRE(expand_groups(members, {"a", "d"}) == std::vector<std::string>{"a", "b", "d"});
}

TEST_CASE("geometry_from_drag_many moves the set as a block", "[canvas][layout]")
{
    const CanvasSize canvas{1920, 1080};
    const std::vector<Rect> start{{"a", 100, 80, 40, 40}, {"b", 200, 90, 50, 30}};
    const auto moved = geometry_from_drag_many(Handle::Move, start, 15, -10, canvas);
    REQUIRE(moved.size() == 2);
    REQUIRE(moved[0].x == Approx(115));
    REQUIRE(moved[0].y == Approx(70));
    REQUIRE(moved[1].x == Approx(215));
    REQUIRE(moved[1].y == Approx(80));
}

TEST_CASE("geometry_from_drag_many scales around the union AABB", "[canvas][layout]")
{
    const CanvasSize canvas{1920, 1080};
    const std::vector<Rect> start{{"a", 100, 100, 100, 100}, {"b", 300, 100, 100, 100}};
    // Union is 100,100,300x100. East-resize by +100 -> union width 400, scale 4/3.
    const auto next = geometry_from_drag_many(Handle::E, start, 100, 0, canvas);
    REQUIRE(next[0].x == Approx(100));
    REQUIRE(next[0].w == Approx(100.0 * 400.0 / 300.0));
    REQUIRE(next[1].x == Approx(100 + (300 - 100) * (400.0 / 300.0)));
    REQUIRE(next[1].w == Approx(100.0 * 400.0 / 300.0));
}

TEST_CASE("geometry_from_drag_many freezes a locked set", "[canvas][layout]")
{
    const CanvasSize canvas{1920, 1080};
    std::vector<Rect> start{{"a", 100, 80, 40, 40}, {"b", 200, 90, 50, 30}};
    start[1].locked = true;
    const auto moved = geometry_from_drag_many(Handle::Move, start, 40, 40, canvas);
    REQUIRE(moved[0].x == Approx(100));
    REQUIRE(moved[1].x == Approx(200));
}

TEST_CASE("align one object to the canvas, several to the union", "[canvas][layout]")
{
    const Rect frame{"", 0, 0, 1000, 800};
    auto one = align_rects({{"a", 40, 50, 100, 80}}, Align::Left, frame);
    REQUIRE(one[0].x == Approx(0));

    auto two = align_rects({{"a", 40, 10, 20, 20}, {"b", 80, 40, 20, 20}}, Align::Left, frame);
    REQUIRE(two[0].x == Approx(40));
    REQUIRE(two[1].x == Approx(40));

    auto mid = align_rects({{"a", 0, 0, 20, 20}, {"b", 80, 40, 20, 20}}, Align::Middle, frame);
    REQUIRE(mid[0].y == Approx(20));
    REQUIRE(mid[1].y == Approx(20));
}

TEST_CASE("distribute spaces the middle boxes evenly", "[canvas][layout]")
{
    std::vector<Rect> rects{{"a", 0, 0, 50, 20}, {"b", 100, 0, 50, 20}, {"c", 300, 0, 50, 20}};
    const auto next = distribute_rects(rects, Distribute::Horizontal);
    REQUIRE(next[1].x == Approx(150));
    REQUIRE(distribute_rects({{"a", 0, 0, 10, 10}, {"b", 20, 0, 10, 10}}, Distribute::Horizontal)[1]
                .x
            == Approx(20));
}

TEST_CASE("nudge clamps to the canvas overhang", "[canvas][layout]")
{
    const CanvasSize canvas{200, 100};
    auto next = nudge_rects({{"a", 10, 10, 40, 24}}, -1000, 0, canvas);
    REQUIRE(next[0].x + next[0].w >= Approx(20));
}

TEST_CASE("flip_rects mirrors around the union centre and toggles flags", "[canvas][layout]")
{
    std::vector<Rect> rects{{"a", 0, 10, 40, 20}, {"b", 80, 10, 40, 20}};
    const auto next = flip_rects(rects, FlipAxis::Horizontal);
    REQUIRE(next[0].x == Approx(80));
    REQUIRE(next[1].x == Approx(0));
    REQUIRE(next[0].flip_h);
    REQUIRE(next[1].flip_h);

    rects[0].rotation = 15;
    const auto spun = flip_rects(rects, FlipAxis::Horizontal);
    REQUIRE(spun[0].rotation == Approx(-15));
    REQUIRE_FALSE(rects_intersect(Rect{"", 0, 0, 10, 10}, Rect{"", 20, 0, 10, 10}));
}

TEST_CASE("point_in_rect follows a clockwise rotation", "[canvas][interact][layout]")
{
    const Rect box{"r", 100, 100, 80, 20, 90};
    REQUIRE(point_in_rect(140, 100, box));
    REQUIRE_FALSE(point_in_rect(170, 110, box));
}

TEST_CASE("snap_many meets the canvas edges with the whole selection", "[canvas][layout]")
{
    const CanvasSize canvas{1920, 1080};
    const auto targets = snap_axis_targets(canvas, {}, {});
    const std::vector<Rect> rects{{"a", 6, 300, 100, 50}, {"b", 206, 400, 100, 50}};

    // Moving: the union's left edge meets 0 and both members shift by the same 6.
    const auto moved = snap_many(rects, Handle::Move, targets, canvas, true);
    REQUIRE(moved.rects.size() == 2);
    CHECK(moved.v_guide == Approx(0));
    CHECK(moved.rects[0].x == Approx(0));
    CHECK(moved.rects[1].x == Approx(200));
    CHECK(moved.rects[1].y == Approx(400));

    // Resizing east: the union's right edge meets 1920 and the members scale.
    const std::vector<Rect> wide{{"a", 100, 300, 100, 50}, {"b", 1800, 400, 114, 50}};
    const auto east = snap_many(wide, Handle::E, targets, canvas, true);
    REQUIRE(east.rects.size() == 2);
    CHECK(east.v_guide == Approx(1920));
    CHECK(east.rects[0].x == Approx(100));
    CHECK(east.rects[1].x + east.rects[1].w == Approx(1920));
}
