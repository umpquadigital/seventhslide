#include <catch2/catch_test_macros.hpp>

#include "pixel_compare.hpp"
#include "scene_object_helpers.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/render/video_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QPainter>

#include <cmath>

using seventhslide::render::paint_video_media;
using seventhslide::render::SourceClock;
using seventhslide::render::SourceFrames;
using seventhslide::render::video_area_rect;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneVideoMedia;

using seventhslide::test::nearly_rgb;

namespace {

SceneFrame make_video_frame()
{
    SceneFrame frame;
    frame.mode = SceneMode::Video;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    SceneVideoMedia media;
    media.source = "content 1";
    media.url = "/static/videos/clip.mp4";
    media.area = {.enabled = true, .x = 0, .y = 0, .width = 100, .height = 100};
    frame.video = media;
    return frame;
}

}  // namespace

TEST_CASE("paint_video_media honors video_enabled", "[render][video]")
{
    seventhslide::test::ensure_qt_application();

    QImage src(40, 20, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(255, 0, 0));

    SceneFrame frame = make_video_frame();
    REQUIRE(video_area_rect(frame) == QRectF(0, 0, 100, 100));
    // Its own video's picture, by its source.
    const SourceFrames frames{{"content 1", {src, {}}}};

    QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);

    SECTION("enabled paints the area")
    {
        out.fill(QColor(0, 255, 0));
        QPainter p(&out);
        paint_video_media(p, frame, &frames);
        p.end();
        // contain into 100x100: 100x50 band at y=25, letterboxed on black.
        REQUIRE(nearly_rgb(out.pixel(50, 50), 255, 0, 0));
        REQUIRE(nearly_rgb(out.pixel(50, 10), 0, 0, 0));
        // Outside the area the underlay survives - the plate is clipped.
        REQUIRE(nearly_rgb(out.pixel(150, 50), 0, 255, 0));
    }

    SECTION("disabled leaves the canvas alone")
    {
        frame.video->area.enabled = false;
        out.fill(QColor(0, 255, 0));
        QPainter p(&out);
        paint_video_media(p, frame, &frames);
        p.end();
        REQUIRE(nearly_rgb(out.pixel(50, 50), 0, 255, 0));
        REQUIRE(nearly_rgb(out.pixel(50, 10), 0, 255, 0));
    }

    SECTION("a disabled area paints nothing at all")
    {
        frame.video->area.enabled = false;

        out.fill(QColor(0, 0, 0));
        QPainter p(&out);
        paint_video_media(p, frame, &frames);
        p.end();

        for (int y = 0; y < out.height(); ++y) {
            for (int x = 0; x < out.width(); ++x) {
                REQUIRE(qRed(out.pixel(x, y)) == 0);
            }
        }
    }
}

namespace {

/// A video frame showing source ``source`` with a countdown clock object.
SceneFrame countdown_frame(const std::string& source)
{
    SceneFrame frame = make_video_frame();
    frame.video->source = source;
    frame.video->area.enabled = false;
    seventhslide::canvas::Object clk = seventhslide::test::scene_clock();
    clk.x = 10;
    clk.y = 10;
    clk.w = 180;
    clk.h = 60;
    clk.font_size = 40;
    clk.font_family = "Liberation Sans";
    clk.font_color = "#ffffff";
    clk.clock_mode = seventhslide::scene::ClockMode::Countdown;
    seventhslide::scene::SceneObject so;
    so.id = "te_countdown";
    so.object = clk;
    frame.objects.push_back(std::move(so));
    return frame;
}

QImage paint_objects(const SceneFrame& frame, const SourceFrames* frames)
{
    QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(0, 0, 0));
    QPainter p(&out);
    seventhslide::render::paint_scene_objects(p, frame, {.source_frames = frames});
    p.end();
    return out;
}

bool any_light(const QImage& img)
{
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qRed(img.pixel(x, y)) > 100) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

// The countdown is a Clock element with its own placement, so it paints with the
// object stack - over a theme that places no video area at all - and counts
// down the video of the frame it is in.
TEST_CASE("a countdown clock object paints the remaining time of its own video", "[render]")
{
    seventhslide::test::ensure_qt_application();

    const SourceFrames frames{{"content 1", {QImage(), SourceClock{5.0, 65.0}}},
                              {"content 2", {QImage(), SourceClock{50.0, 65.0}}}};
    const QImage one = paint_objects(countdown_frame("content 1"), &frames);
    REQUIRE(any_light(one));
    // Another video, with another time left: another picture.
    const QImage two = paint_objects(countdown_frame("content 2"), &frames);
    REQUIRE(any_light(two));
    CHECK(one != two);
    // Its own video, again: the same.
    CHECK(paint_objects(countdown_frame("content 1"), &frames) == one);
    // No clock for it (not played yet, or the theme designer's): the sample,
    // which is neither.
    const QImage sample = paint_objects(countdown_frame("content 3"), &frames);
    REQUIRE(any_light(sample));
    CHECK(sample != one);
    CHECK(sample != two);
}

// A change between two slides with ids (a theme's elements have them) paints
// object by object; each side's countdown still reads its own side's video.
TEST_CASE("a countdown in a change counts down its own side's video", "[render][transition]")
{
    seventhslide::test::ensure_qt_application();

    const SourceFrames frames{{"content 1", {QImage(), SourceClock{5.0, 65.0}}},
                              {"content 2", {QImage(), SourceClock{50.0, 65.0}}}};
    const SceneFrame going = countdown_frame("content 1");
    const SceneFrame coming = countdown_frame("content 2");
    const auto changing = [&](double progress) {
        QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);
        out.fill(QColor(0, 0, 0));
        QPainter p(&out);
        seventhslide::render::paint_objects_transitioning(
            p, going, coming, progress, seventhslide::model::transition_from_fade(true, 500),
            {.source_frames = &frames}, 0.0);
        p.end();
        return out;
    };
    // Come in: the coming side's own time, not the sample.
    CHECK(changing(1.0) == paint_objects(coming, &frames));
    CHECK(changing(1.0) != paint_objects(countdown_frame("content 3"), &frames));
}
