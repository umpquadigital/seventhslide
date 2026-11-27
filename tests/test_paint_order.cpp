#include <catch2/catch_test_macros.hpp>

#include <seventhslide/scene/paint_order.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using seventhslide::scene::ChangeOrder;
using seventhslide::scene::PaintPart;
using seventhslide::scene::PaintSide;
using seventhslide::scene::PaintStep;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneObject;
using seventhslide::scene::paint_order;
using seventhslide::scene::paints_media;

namespace {

SceneObject object_at(std::string id, int z)
{
    SceneObject obj;
    obj.id = std::move(id);
    obj.z = z;
    return obj;
}

/// A picture in an area at ``area_z``, with objects at the given z.
SceneFrame picture_frame(int area_z, std::vector<SceneObject> objects)
{
    SceneFrame frame;
    frame.mode = SceneMode::Image;
    frame.image = seventhslide::scene::SceneImageMedia{
        .url = "/static/images/a.png", .area = {.enabled = true, .z = area_z}};
    frame.objects = std::move(objects);
    return frame;
}

/// A song slide whose lyrics are at ``body_z``, with objects at the given z.
SceneFrame song_frame(int body_z, std::vector<SceneObject> objects)
{
    SceneFrame frame;
    frame.mode = SceneMode::Song;
    seventhslide::scene::SceneTextContent text;
    text.z = body_z;
    text.lines.emplace_back();
    frame.text = std::move(text);
    frame.objects = std::move(objects);
    return frame;
}

/// What each step is, as ``part:side:z``, so a failure reads as the order.
std::vector<std::string> describe(const std::vector<PaintStep>& steps)
{
    std::vector<std::string> out;
    for (const auto& step : steps) {
        const char* part = step.part == PaintPart::Body       ? "body"
                           : step.part == PaintPart::TitleBox ? "title"
                           : step.part == PaintPart::Media    ? "media"
                                                              : "object";
        const char* side = step.side == PaintSide::Outgoing   ? "out"
                           : step.side == PaintSide::Incoming ? "in"
                                                              : "both";
        out.push_back(std::string(part) + ":" + side + ":" + std::to_string(step.z));
    }
    return out;
}

}  // namespace

TEST_CASE("A frame's media paints at its area's place among the objects",
          "[scene][paint_order]")
{
    // Main's "Media Default": the image area at 10, a shape at 20 and a text
    // at 30 over it, and here an object at 5 under it.
    const auto frame =
        picture_frame(10, {object_at("te10", 20), object_at("under", 5), object_at("te11", 30)});
    CHECK(describe(paint_order(frame))
          == std::vector<std::string>{"object:in:5", "media:in:10", "object:in:20",
                                      "object:in:30"});
}

TEST_CASE("Video, web and live sources paint at their area's place too", "[scene][paint_order]")
{
    SceneFrame frame;
    frame.objects = {object_at("clock", 30)};

    SECTION("video")
    {
        frame.mode = SceneMode::Video;
        frame.video = seventhslide::scene::SceneVideoMedia{.area = {.enabled = true, .z = 0}};
        CHECK(describe(paint_order(frame))
              == std::vector<std::string>{"media:in:0", "object:in:30"});
    }
    SECTION("web: a page over its clock hides it")
    {
        frame.mode = SceneMode::Web;
        frame.web = seventhslide::scene::SceneWebMedia{.area = {.enabled = true, .z = 40}};
        CHECK(describe(paint_order(frame))
              == std::vector<std::string>{"object:in:30", "media:in:40"});
    }
    SECTION("live")
    {
        frame.mode = SceneMode::Live;
        frame.live = seventhslide::scene::SceneLiveMedia{.area = {.enabled = true, .z = 0}};
        CHECK(describe(paint_order(frame))
              == std::vector<std::string>{"media:in:0", "object:in:30"});
    }
}

TEST_CASE("Media paints only in its own mode, in an area the theme gives it",
          "[scene][paint_order]")
{
    auto frame = picture_frame(10, {});
    CHECK(paints_media(frame));
    frame.image->area.enabled = false;
    CHECK_FALSE(paints_media(frame));
    frame.image->area.enabled = true;
    frame.mode = SceneMode::Song;
    CHECK_FALSE(paints_media(frame));
    frame.mode = SceneMode::Image;
    frame.is_blank = true;
    CHECK_FALSE(paints_media(frame));
    CHECK(paint_order(frame).empty());
}

TEST_CASE("The body and title boxes paint at their elements' places", "[scene][paint_order]")
{
    // A shape sent behind the lyrics is behind them, and one in front is in
    // front.
    auto frame = song_frame(10, {object_at("front", 20), object_at("behind", 0)});
    seventhslide::layout::OverlayBox title;
    title.z = 15;
    frame.overlay.boxes.push_back(title);
    CHECK(describe(paint_order(frame))
          == std::vector<std::string>{"object:in:0", "body:in:10", "title:in:15",
                                      "object:in:20"});
}

TEST_CASE("On equal z the body, title boxes and media paint under the objects",
          "[scene][paint_order]")
{
    auto frame = picture_frame(10, {object_at("tie", 10)});
    CHECK(describe(paint_order(frame)) == std::vector<std::string>{"media:in:10", "object:in:10"});
    frame = song_frame(10, {object_at("tie", 10)});
    CHECK(describe(paint_order(frame)) == std::vector<std::string>{"body:in:10", "object:in:10"});
}

TEST_CASE("A change shares objects by id and the same media", "[scene][paint_order]")
{
    const auto outgoing = picture_frame(10, {object_at("te10", 20), object_at("gone", 25)});
    auto incoming = picture_frame(10, {object_at("te10", 20), object_at("new", 30)});
    incoming.image->url = "/static/images/b.png";

    SECTION("another picture: one media step per side, the outgoing first")
    {
        const auto steps =
            paint_order(outgoing, incoming, {.match_objects = true, .same_media = false});
        CHECK(describe(steps)
              == std::vector<std::string>{"media:out:10", "media:in:10", "object:both:20",
                                          "object:out:25", "object:in:30"});
        REQUIRE(steps[2].side == PaintSide::Both);
        CHECK(steps[2].index == 0);
        CHECK(steps[2].outgoing_index == 0);
        CHECK(steps[3].index == 1);  // the outgoing frame's own index
    }
    SECTION("the same media: one step, at the incoming z")
    {
        auto moved = incoming;
        moved.image->area.z = 22;
        const auto steps = paint_order(outgoing, moved, {.match_objects = true, .same_media = true});
        CHECK(describe(steps)
              == std::vector<std::string>{"object:both:20", "media:both:22", "object:out:25",
                                          "object:in:30"});
    }
    SECTION("without matching, every object is its frame's")
    {
        const auto steps =
            paint_order(outgoing, incoming, {.match_objects = false, .same_media = false});
        CHECK(describe(steps)
              == std::vector<std::string>{"media:out:10", "media:in:10", "object:in:20",
                                          "object:out:20", "object:out:25", "object:in:30"});
    }
}

TEST_CASE("A shared object takes the incoming z, and one gone by an exit is not shared",
          "[scene][paint_order]")
{
    auto outgoing = song_frame(0, {object_at("a", 5), object_at("b", 6)});
    auto& exit_build = outgoing.objects[1].builds.emplace_back();
    exit_build.effect = seventhslide::canvas::AnimationEffect::FadeOut;
    REQUIRE(outgoing.objects[1].exits());
    const auto incoming = song_frame(0, {object_at("a", 40), object_at("b", 6)});
    const auto steps = paint_order(outgoing, incoming, {.match_objects = true});
    CHECK(describe(steps)
          == std::vector<std::string>{"body:out:0", "body:in:0", "object:in:6", "object:out:6",
                                      "object:both:40"});
}

TEST_CASE("A blank side of a change paints nothing", "[scene][paint_order]")
{
    auto outgoing = picture_frame(10, {object_at("te10", 20)});
    outgoing.is_blank = true;
    const auto incoming = song_frame(0, {object_at("te10", 20)});
    CHECK(describe(paint_order(outgoing, incoming, {.match_objects = true, .same_media = true}))
          == std::vector<std::string>{"body:in:0", "object:in:20"});
}

TEST_CASE("On equal z the body is under the title boxes, and both under the media",
          "[scene][paint_order]")
{
    // A hand-edited file's tie: the body, then a title box, then the media,
    // then the objects.
    auto frame = picture_frame(10, {object_at("tie", 10)});
    seventhslide::scene::SceneTextContent text;
    text.z = 10;
    text.lines.emplace_back();
    frame.text = std::move(text);
    seventhslide::layout::OverlayBox title;
    title.z = 10;
    frame.overlay.boxes.push_back(title);
    CHECK(describe(paint_order(frame))
          == std::vector<std::string>{"body:in:10", "title:in:10", "media:in:10",
                                      "object:in:10"});
}

TEST_CASE("A blank frame paints nothing, its objects included", "[scene][paint_order]")
{
    auto frame = song_frame(0, {object_at("te10", 20)});
    frame.is_blank = true;
    CHECK(paint_order(frame).empty());
}

TEST_CASE("Matching by id shares neither objects without one nor a second of one id",
          "[scene][paint_order]")
{
    const auto outgoing = song_frame(0, {object_at("", 5), object_at("a", 6), object_at("a", 7)});
    const auto incoming = song_frame(0, {object_at("", 5), object_at("a", 8), object_at("a", 9)});
    const auto steps = paint_order(outgoing, incoming, {.match_objects = true});
    CHECK(describe(steps)
          == std::vector<std::string>{"body:out:0", "body:in:0", "object:in:5", "object:out:5",
                                      "object:out:7", "object:both:8", "object:in:9"});
    // The first of each frame's "a" is the one they share.
    const auto both = std::find_if(steps.begin(), steps.end(),
                                   [](const PaintStep& s) { return s.side == PaintSide::Both; });
    REQUIRE(both != steps.end());
    CHECK(both->index == 1);
    CHECK(both->outgoing_index == 1);
}

TEST_CASE("The same media is shared only when both sides paint it", "[scene][paint_order]")
{
    const auto outgoing = picture_frame(10, {});
    auto incoming = picture_frame(10, {});
    incoming.image->area.enabled = false;
    CHECK(describe(paint_order(outgoing, incoming, {.same_media = true}))
          == std::vector<std::string>{"media:out:10"});
    CHECK(describe(paint_order(incoming, outgoing, {.same_media = true}))
          == std::vector<std::string>{"media:in:10"});
}
