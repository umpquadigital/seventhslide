// Every element of a design paints at its own place in the stack, whatever
// fills it at showtime: the picture, video, page or live source in its area,
// and the lyrics or passage in their box. These check pixels at known spots,
// settled and through every kind of change.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "pixel_compare.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/source_frames.hpp>
#include <seventhslide/scene/scene_builder.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

using seventhslide::render::BackgroundImageCache;
using seventhslide::render::SceneFadeLayers;
using seventhslide::render::ScenePresentation;
using seventhslide::render::SceneResources;
using seventhslide::render::SourceFrames;
using seventhslide::render::render_scene_frame_image;
using seventhslide::render::render_scene_presentation_image;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneObject;
using seventhslide::test::nearly_rgb;

namespace {

constexpr int k_w = 200;
constexpr int k_h = 100;
/// Where the shape is: well inside it, and well inside the canvas.
constexpr int k_spot_x = 40;
constexpr int k_spot_y = 50;

/// A picture file, decoded through a cache, and the frames' video, page and
/// live sources, every one solid red.
struct Media {
    QTemporaryDir dir;
    BackgroundImageCache cache;
    SourceFrames sources;

    Media()
    {
        REQUIRE(dir.isValid());
        const auto root = std::filesystem::path(dir.path().toStdString());
        std::filesystem::create_directories(root / "images");
        for (const auto& [name, colour] :
             {std::pair{"red", QColor(255, 0, 0)}, std::pair{"blue", QColor(0, 0, 255)}}) {
            QImage picture(40, 20, QImage::Format_ARGB32_Premultiplied);
            picture.fill(colour);
            REQUIRE(picture.save(
                QString::fromStdString((root / "images" / (std::string(name) + ".png")).string())));
        }
        cache.set_static_root(root);
        QImage red(k_w, k_h, QImage::Format_ARGB32_Premultiplied);
        red.fill(QColor(255, 0, 0));
        for (const char* source : {"video", "page", "camera"}) {
            sources[source].image = red;
        }
    }

    [[nodiscard]] SceneResources resources()
    {
        SceneResources res;
        res.cache = &cache;
        res.source_frames = &sources;
        return res;
    }
};

[[nodiscard]] SceneObject green_shape(std::string id, int z)
{
    SceneObject obj;
    obj.id = std::move(id);
    obj.z = z;
    seventhslide::canvas::Object shape;
    shape.type = seventhslide::canvas::ObjectType::Shape;
    shape.x = 20;
    shape.y = 30;
    shape.w = 40;
    shape.h = 40;
    shape.fill.color = "#00ff00";
    obj.object = shape;
    return obj;
}

/// ``mode``'s media filling the canvas in an area at ``area_z``, the picture
/// being ``picture`` (an Image frame), with a black background.
[[nodiscard]] SceneFrame media_frame(SceneMode mode, int area_z, const char* picture = "red")
{
    SceneFrame frame;
    frame.mode = mode;
    frame.canvas_width = k_w;
    frame.canvas_height = k_h;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    const seventhslide::scene::SceneMediaArea area{.enabled = true, .z = area_z};
    switch (mode) {
    case SceneMode::Image:
        frame.image = seventhslide::scene::SceneImageMedia{
            .url = "/static/images/" + std::string(picture) + ".png", .area = area, .fit = "fill"};
        break;
    case SceneMode::Video:
        frame.video = seventhslide::scene::SceneVideoMedia{
            .source = "video", .url = "/static/videos/clip.mp4", .area = area};
        break;
    case SceneMode::Web:
        frame.web = seventhslide::scene::SceneWebMedia{
            .source = "page", .url = "https://example.org", .area = area, .fit = "fill"};
        break;
    case SceneMode::Live:
        frame.live = seventhslide::scene::SceneLiveMedia{.source = "camera", .area = area};
        break;
    default:
        FAIL("not a media mode");
    }
    return frame;
}

/// A song slide whose lyrics, a block of white "H"s filling the canvas, are
/// at ``body_z``.
[[nodiscard]] SceneFrame song_frame(int body_z)
{
    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = k_w;
    frame.canvas_height = k_h;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    seventhslide::scene::SceneTextContent text;
    text.z = body_z;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = k_w;
    text.box.height = k_h;
    text.box.font_family = "Liberation Sans";
    text.box.font_size = 90;
    text.box.color = "#ffffff";
    text.box.align = "center";
    text.box.valign = "middle";
    seventhslide::scene::SceneTextLine line;
    line.runs = seventhslide::model::RichLine{seventhslide::model::TextSpan{"HHHHHH", {}}};
    text.lines.push_back(std::move(line));
    frame.text = std::move(text);
    return frame;
}

[[nodiscard]] int white_in_shape(const QImage& img)
{
    int white = 0;
    for (int y = 32; y < 68; ++y) {
        for (int x = 22; x < 58; ++x) {
            if (nearly_rgb(img.pixel(x, y), 255, 255, 255, 40)) {
                ++white;
            }
        }
    }
    return white;
}

/// Pixels inside the green shape that are not pure green: words showing on
/// it, however faintly (a dissolve's words are part strength).
[[nodiscard]] int words_on_shape(const QImage& img)
{
    int words = 0;
    for (int y = 32; y < 68; ++y) {
        for (int x = 22; x < 58; ++x) {
            const QRgb px = img.pixel(x, y);
            if (qRed(px) > 30 || qBlue(px) > 30) {
                ++words;
            }
        }
    }
    return words;
}

/// ``pres`` painted, with fade scratch or without: the two must agree.
[[nodiscard]] QImage paint(const ScenePresentation& pres, SceneResources res, bool scratch)
{
    SceneFadeLayers layers;
    if (scratch) {
        res.fade_layers = &layers;
    }
    return render_scene_presentation_image(pres, k_w, k_h, QColor(Qt::black), res);
}

/// ``pres`` painted with ``layers`` as its scratch, kept from call to call as
/// a change keeps it.
[[nodiscard]] QImage paint_with(const ScenePresentation& pres,
                                SceneResources res,
                                SceneFadeLayers& layers)
{
    res.fade_layers = &layers;
    return render_scene_presentation_image(pres, k_w, k_h, QColor(Qt::black), res);
}

/// A title box of white "HHH" filling the canvas, at ``z``.
[[nodiscard]] seventhslide::layout::OverlayBox title_box(int z)
{
    seventhslide::layout::OverlayBox box;
    box.x = 0;
    box.y = 0;
    box.w = k_w;
    box.h = k_h;
    box.font_family = "Liberation Sans";
    box.font_size = 60;
    box.font_color = "#ffffff";
    box.text_align = "center";
    box.vertical_align = "middle";
    // Short enough to stay on one line: a title box wraps to its width.
    box.lines.push_back({seventhslide::model::TextSpan{"HHH", {}}});
    box.z = z;
    return box;
}

/// ``shape`` moved to ``x``.
[[nodiscard]] SceneObject at_x(SceneObject shape, double x)
{
    shape.object->x = x;
    return shape;
}

}  // namespace

TEST_CASE("A theme's objects paint over or under the media by their place in the stack",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    const auto mode = GENERATE(SceneMode::Image, SceneMode::Video, SceneMode::Web, SceneMode::Live);
    CAPTURE(static_cast<int>(mode));

    SECTION("over the area, it shows")
    {
        auto frame = media_frame(mode, 10);
        frame.objects = {green_shape("te10", 20)};
        const QImage img = render_scene_frame_image(frame, k_w, k_h, Qt::black, media.resources());
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
        // The media is there around it.
        CHECK(nearly_rgb(img.pixel(150, 50), 255, 0, 0));
    }
    SECTION("under the area, the media covers it")
    {
        auto frame = media_frame(mode, 10);
        frame.objects = {green_shape("te10", 5)};
        const QImage img = render_scene_frame_image(frame, k_w, k_h, Qt::black, media.resources());
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 255, 0, 0));
    }
    SECTION("on equal z, the media is under")
    {
        auto frame = media_frame(mode, 10);
        frame.objects = {green_shape("te10", 10)};
        const QImage img = render_scene_frame_image(frame, k_w, k_h, Qt::black, media.resources());
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
    }
}

TEST_CASE("A design preview's media stand-in paints at its area's place in the stack",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    const auto mode = GENERATE(SceneMode::Image, SceneMode::Video, SceneMode::Web, SceneMode::Live);
    CAPTURE(static_cast<int>(mode));
    // The area over the left half, nothing playing in it, on a white canvas.
    const auto stand_in_frame = [&](int shape_z, bool bars = false) {
        SceneFrame frame;
        frame.mode = mode;
        frame.canvas_width = k_w;
        frame.canvas_height = k_h;
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#ffffff";
        const seventhslide::scene::SceneMediaArea area{
            .enabled = true, .x = 0, .y = 0, .width = k_w / 2, .height = k_h, .z = 10};
        switch (mode) {
        case SceneMode::Image:
            frame.image = seventhslide::scene::SceneImageMedia{.area = area};
            break;
        case SceneMode::Video:
            frame.video = seventhslide::scene::SceneVideoMedia{.area = area};
            break;
        case SceneMode::Web:
            frame.web = seventhslide::scene::SceneWebMedia{.area = area};
            break;
        default:
            frame.live = seventhslide::scene::SceneLiveMedia{.area = area};
            break;
        }
        frame.media_stand_in = seventhslide::scene::SceneMediaStandIn{
            .bars = bars,
            .label = seventhslide::scene::stand_in_label(
                "Picture", {.x = 0, .y = 0, .w = k_w / 2.0, .h = k_h}, {k_w, k_h})};
        frame.objects = {green_shape("te10", shape_z)};
        return render_scene_frame_image(frame, k_w, k_h, Qt::black);
    };
    // Away from the label in the middle of the area.
    constexpr int k_x = 24;
    constexpr int k_y = 34;

    SECTION("an object under the area is behind the output's plate")
    {
        const QImage img = stand_in_frame(5);
        CHECK(nearly_rgb(img.pixel(k_x, k_y), 0, 0, 0));
        // The plate keeps to its area.
        CHECK(nearly_rgb(img.pixel(150, 50), 255, 255, 255));
    }
    SECTION("an object over the area shows on the plate")
    {
        const QImage img = stand_in_frame(20);
        CHECK(nearly_rgb(img.pixel(k_x, k_y), 0, 255, 0));
        CHECK(nearly_rgb(img.pixel(80, 10), 0, 0, 0));
    }
    SECTION("colour bars cover what is under the area too, and keep to it")
    {
        const QImage under = stand_in_frame(5, true);
        // The area is 100 px wide, so the spot is on the second, yellow, bar.
        CHECK(nearly_rgb(under.pixel(k_x, k_y), 191, 191, 0));
        CHECK(nearly_rgb(under.pixel(150, 50), 255, 255, 255));
        const QImage over = stand_in_frame(20, true);
        CHECK(nearly_rgb(over.pixel(k_x, k_y), 0, 255, 0));
    }
}

TEST_CASE("Colour bars fit an area of any size and shape", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    // Wide, tall and small, each placed off the image's corner.
    const QRect areas[] = {{10, 20, 700, 300}, {5, 5, 90, 400}, {3, 4, 21, 12}};
    for (const QRect& area : areas) {
        CAPTURE(area.width(), area.height());
        QImage img(area.right() + 10, area.bottom() + 10, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            seventhslide::render::paint_colour_bars(p, QRectF(area));
        }
        // The middle of a share of the area, as a pixel.
        const auto at = [&](double x, double y) {
            return img.pixel(area.left() + static_cast<int>(area.width() * x),
                             area.top() + static_cast<int>(area.height() * y));
        };
        // The bars are flat colour: each is exactly its level.
        const auto is = [](QRgb px, int r, int g, int b) { return nearly_rgb(px, r, g, b, 1); };
        // The seven bars across the top two thirds.
        CHECK(is(at(0.5 / 7, 0.3), 191, 191, 191));
        CHECK(is(at(1.5 / 7, 0.3), 191, 191, 0));
        CHECK(is(at(2.5 / 7, 0.3), 0, 191, 191));
        CHECK(is(at(3.5 / 7, 0.3), 0, 191, 0));
        CHECK(is(at(4.5 / 7, 0.3), 191, 0, 191));
        CHECK(is(at(5.5 / 7, 0.3), 191, 0, 0));
        CHECK(is(at(6.5 / 7, 0.3), 0, 0, 191));
        if (area.height() >= 60) {
            // The reversed row, its bars between set-up black.
            CHECK(is(at(0.5 / 7, 0.71), 0, 0, 191));
            CHECK(is(at(1.5 / 7, 0.71), 19, 19, 19));
            CHECK(is(at(2.5 / 7, 0.71), 191, 0, 191));
            CHECK(is(at(6.5 / 7, 0.71), 191, 191, 191));
            // The foot: -I, white, +Q and black.
            CHECK(is(at(2.5 / 28, 0.9), 0, 33, 76));
            CHECK(is(at(7.5 / 28, 0.9), 255, 255, 255));
            CHECK(is(at(12.5 / 28, 0.9), 50, 0, 106));
            CHECK(is(at(17.5 / 28, 0.9), 19, 19, 19));
            CHECK(is(at(6.5 / 7, 0.9), 19, 19, 19));
        }
        if (area.width() >= 200) {
            // The PLUGE: 4 IRE below black, black, and 4 IRE above.
            CHECK(is(at(5.0 / 7 + 0.5 / 21, 0.9), 9, 9, 9));
            CHECK(is(at(5.0 / 7 + 1.5 / 21, 0.9), 19, 19, 19));
            CHECK(is(at(5.0 / 7 + 2.5 / 21, 0.9), 29, 29, 29));
        }
        // Every pixel of the area is painted, and none past it on any side.
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                REQUIRE(qAlpha(img.pixel(x, y)) == 255);
            }
        }
        for (int y = area.top() - 1; y <= area.bottom() + 1; ++y) {
            REQUIRE(qAlpha(img.pixel(area.left() - 1, y)) == 0);
            REQUIRE(qAlpha(img.pixel(area.right() + 1, y)) == 0);
        }
        for (int x = area.left() - 1; x <= area.right() + 1; ++x) {
            REQUIRE(qAlpha(img.pixel(x, area.top() - 1)) == 0);
            REQUIRE(qAlpha(img.pixel(x, area.bottom() + 1)) == 0);
        }
    }
}

TEST_CASE("Colour bars leave no gap at a fractional place and scale", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    // As the compositor paints them: on the canvas, scaled to the output.
    QImage img(200, 120, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const QRectF area(3.3, 4.7, 100.2, 60.1);
    const QTransform fit = QTransform::fromTranslate(0.4, 0.3).scale(1.37, 1.37);
    {
        QPainter p(&img);
        p.setTransform(fit);
        seventhslide::render::paint_colour_bars(p, area);
    }
    const QRect on_image = fit.mapRect(area).toAlignedRect();
    // Inside the area's edges, every pixel is painted.
    for (int y = on_image.top() + 1; y < on_image.bottom(); ++y) {
        for (int x = on_image.left() + 1; x < on_image.right(); ++x) {
            REQUIRE(qAlpha(img.pixel(x, y)) == 255);
        }
    }
    // Past them, none is.
    for (int y = 0; y < img.height(); ++y) {
        REQUIRE(qAlpha(img.pixel(on_image.right() + 1, y)) == 0);
    }
    for (int x = 0; x < img.width(); ++x) {
        REQUIRE(qAlpha(img.pixel(x, on_image.bottom() + 1)) == 0);
    }
}

TEST_CASE("Colour bars in an area too small to see paint at most it", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    QImage img(10, 10, QImage::Format_ARGB32_Premultiplied);
    const auto painted = [&](const QRectF& area) {
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            seventhslide::render::paint_colour_bars(p, area);
        }
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                n += qAlpha(img.pixel(x, y)) != 0 ? 1 : 0;
            }
        }
        return n;
    };
    CHECK(painted(QRectF(4, 4, 1, 1)) == 1);
    CHECK(painted(QRectF(4, 4, 0, 5)) == 0);
    CHECK(painted(QRectF(4, 4, 5, 0)) == 0);
}

TEST_CASE("A media stand-in's label reads in the middle of its area", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    constexpr int w = 960;
    constexpr int h = 540;
    const auto paint_stand_in = [&](bool bars) {
        SceneFrame frame;
        frame.mode = SceneMode::Image;
        frame.canvas_width = w;
        frame.canvas_height = h;
        frame.image = seventhslide::scene::SceneImageMedia{
            .area = {.enabled = true, .x = 0, .y = 0, .width = w, .height = h}};
        auto label = seventhslide::scene::stand_in_label(
            "Picture", {.x = 0, .y = 0, .w = double(w), .h = double(h)}, {w, h});
        if (bars) {
            label.font_color = "#ffffff";
            label.effects.text_outline = {.color = "#000000", .width = 2};
        }
        frame.media_stand_in = seventhslide::scene::SceneMediaStandIn{.bars = bars, .label = label};
        return render_scene_frame_image(frame, w, h, Qt::black);
    };
    // Pixels in a band across the middle of the top row of bars, where the
    // label is, that ``wanted`` picks.
    const auto count = [](const QImage& img, auto wanted) {
        int n = 0;
        for (int y = h / 2 - 40; y < h / 2 + 40; ++y) {
            for (int x = w / 4; x < 3 * w / 4; ++x) {
                n += wanted(img.pixel(x, y)) ? 1 : 0;
            }
        }
        return n;
    };
    // On the plate: grey words on black.
    const QImage plate = paint_stand_in(false);
    CHECK(count(plate, [](QRgb px) { return qGreen(px) > 100; }) > 50);
    // On bars, which are no brighter than 191: white words, and their black
    // outline.
    const QImage bars = paint_stand_in(true);
    CHECK(count(bars, [](QRgb px) { return nearly_rgb(px, 255, 255, 255, 20); }) > 50);
    CHECK(count(bars, [](QRgb px) { return nearly_rgb(px, 0, 0, 0, 20); }) > 50);
}

TEST_CASE("The lyrics paint over or under a theme's objects by their place in the stack",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();

    auto frame = song_frame(10);
    SECTION("a shape behind the lyrics: the words show over it")
    {
        frame.objects = {green_shape("behind", 0)};
        const QImage img = render_scene_frame_image(frame, k_w, k_h);
        CHECK(white_in_shape(img) > 20);
    }
    SECTION("a shape in front of the lyrics covers them")
    {
        frame.objects = {green_shape("front", 20)};
        const QImage img = render_scene_frame_image(frame, k_w, k_h);
        CHECK(white_in_shape(img) == 0);
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
    }
}

TEST_CASE("A title box paints at its place in the stack", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame = song_frame(0);
    frame.text.reset();
    frame.overlay.boxes.push_back(title_box(10));

    frame.objects = {green_shape("behind", 0)};
    CHECK(white_in_shape(render_scene_frame_image(frame, k_w, k_h)) > 20);
    frame.objects = {green_shape("front", 20)};
    CHECK(white_in_shape(render_scene_frame_image(frame, k_w, k_h)) == 0);
}

TEST_CASE("Objects over the media stay over it through a dissolve", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    const bool scratch = GENERATE(false, true);
    CAPTURE(scratch);

    SECTION("a song to a picture: the shape comes in over the picture")
    {
        const SceneFrame outgoing = song_frame(0);
        auto incoming = media_frame(SceneMode::Image, 10);
        incoming.objects = {green_shape("te10", 20)};
        ScenePresentation pres = ScenePresentation::still(incoming);
        pres.outgoing = &outgoing;
        for (const double progress : {0.5, 1.0}) {
            pres.content_progress = progress;
            INFO("progress " << progress);
            // The shape has no transition of its own: it is simply there, and
            // the picture coming in under it does not cover it.
            const QImage img = paint(pres, media.resources(), scratch);
            CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
        }
    }
    SECTION("one picture to another under one theme: the shape holds over both")
    {
        auto outgoing = media_frame(SceneMode::Image, 10, "blue");
        outgoing.objects = {green_shape("te10", 20)};
        auto incoming = media_frame(SceneMode::Image, 10, "red");
        incoming.objects = {green_shape("te10", 20)};
        ScenePresentation pres = ScenePresentation::still(incoming);
        pres.outgoing = &outgoing;
        pres.content_progress = 0.5;
        const QImage img = paint(pres, media.resources(), scratch);
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
        // The pictures cross under it: neither is all there.
        const QRgb crossing = img.pixel(150, 50);
        CHECK(qRed(crossing) > 60);
        CHECK(qBlue(crossing) > 10);
    }
    SECTION("the same picture through a change of objects plays on at full strength")
    {
        auto outgoing = media_frame(SceneMode::Image, 10);
        auto incoming = media_frame(SceneMode::Image, 10);
        incoming.objects = {green_shape("te10", 20)};
        ScenePresentation pres = ScenePresentation::still(incoming);
        pres.outgoing = &outgoing;
        pres.content_progress = 0.5;
        const QImage img = paint(pres, media.resources(), scratch);
        CHECK(nearly_rgb(img.pixel(150, 50), 255, 0, 0));
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
    }
}

TEST_CASE("Objects without ids keep their place around the media through a dissolve",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    // No ids: each side's objects dissolve with it, drawn in runs around the
    // media rather than as objects of their own.
    auto outgoing = media_frame(SceneMode::Image, 10, "blue");
    outgoing.objects = {green_shape("", 20), green_shape("", 5)};
    auto incoming = media_frame(SceneMode::Image, 10, "red");
    incoming.objects = {green_shape("", 20), green_shape("", 5)};
    incoming.objects[1].object->x = 120;  // under the picture: hidden
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;

    // One scratch for the whole change, as the presenter keeps it: its
    // pictures, two per side, are drawn once and blended on every frame.
    SceneFadeLayers layers;
    for (const double progress : {0.25, 0.5, 0.75, 1.0}) {
        pres.content_progress = progress;
        INFO("progress " << progress);
        const QImage live = paint(pres, media.resources(), false);
        const QImage cached = paint_with(pres, media.resources(), layers);
        CHECK(layers.runs.size() == 4);
        CHECK(seventhslide::test::mean_abs_diff(live, cached) < 1.5);
        // Each side's shape goes and comes with its side, over the pictures:
        // under them, nothing of it would show.
        const QRgb over = cached.pixel(k_spot_x, k_spot_y);
        CHECK(qGreen(over) > 2 * std::max(qRed(over), qBlue(over)));
        if (progress == 1.0) {
            CHECK(nearly_rgb(over, 0, 255, 0));
        }
        CHECK(qGreen(cached.pixel(140, 50)) < 40);
    }
}

TEST_CASE("Objects over the media stay over it through a moving change",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    const bool scratch = GENERATE(false, true);
    CAPTURE(scratch);

    const SceneFrame outgoing = song_frame(0);
    auto incoming = media_frame(SceneMode::Image, 10);
    incoming.objects = {green_shape("te10", 20)};
    incoming.content_transition = {.type = seventhslide::model::TransitionType::Push,
                                   .duration_ms = 500,
                                   .direction = seventhslide::model::TransitionDirection::Right};
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    pres.content_type = seventhslide::model::TransitionType::Push;
    pres.content_progress = 0.5;
    // Pushed in from the right, half way: the incoming slide's left half is
    // the viewport's right half, the shape moving with it.
    const QImage img = paint(pres, media.resources(), scratch);
    CHECK(nearly_rgb(img.pixel(k_w / 2 + k_spot_x, k_spot_y), 0, 255, 0));
    CHECK(nearly_rgb(img.pixel(k_w / 2 + 80, k_spot_y), 255, 0, 0));
}

TEST_CASE("A change during a change keeps the incoming side's order", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    auto incoming = media_frame(SceneMode::Image, 10);
    incoming.objects = {green_shape("te10", 20)};
    const SceneFrame outgoing = song_frame(0);
    QImage carried(k_w, k_h, QImage::Format_ARGB32_Premultiplied);
    carried.fill(QColor(0, 0, 255));
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    pres.outgoing_picture = &carried;

    // Half way: the carried picture going under the incoming side, whose
    // shape is over its own picture.
    pres.content_progress = 0.5;
    const QImage half = paint(pres, media.resources(), true);
    const QRgb shape = half.pixel(k_spot_x, k_spot_y);
    CHECK(qGreen(shape) > 100);
    CHECK(qRed(shape) < 30);
    CHECK(qBlue(shape) > 30);
    const QRgb picture = half.pixel(150, 50);
    CHECK(qRed(picture) > 100);
    CHECK(qGreen(picture) < 30);
    CHECK(qBlue(picture) > 30);

    pres.content_progress = 1.0;
    const QImage img = paint(pres, media.resources(), true);
    CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
    CHECK(nearly_rgb(img.pixel(150, 50), 255, 0, 0));
}

TEST_CASE("An object building in over the media builds over it", "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    const SceneFrame outgoing = song_frame(0);
    auto incoming = media_frame(SceneMode::Image, 10);
    incoming.objects = {green_shape("te10", 20)};
    auto& build = incoming.objects[0].builds.emplace_back();
    build.effect = seventhslide::canvas::AnimationEffect::Appear;
    build.start_ms = 0;
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    pres.content_progress = 1.0;
    pres.elapsed_ms = 100;
    const QImage img = paint(pres, media.resources(), true);
    CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
}

TEST_CASE("The lyrics keep their place among the objects through a dissolve",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    const bool scratch = GENERATE(false, true);
    CAPTURE(scratch);

    auto outgoing = song_frame(10);
    auto incoming = song_frame(10);
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    for (const double progress : {0.5, 1.0}) {
        pres.content_progress = progress;
        INFO("progress " << progress);
        // Half way, both sides' words are there at half strength: in front
        // of the shape, none of either shows on it.
        outgoing.objects = {green_shape("front", 20)};
        incoming.objects = {green_shape("front", 20)};
        CHECK(words_on_shape(paint(pres, {}, scratch)) == 0);
        outgoing.objects = {green_shape("behind", 0)};
        incoming.objects = {green_shape("behind", 0)};
        CHECK(words_on_shape(paint(pres, {}, scratch)) > 20);
    }
}

TEST_CASE("A side's own shape over its lyrics hides them through a dissolve",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    // No ids: the lyrics and the shape over them are one run of each side,
    // which blends as one. Painting the run's steps one by one at half
    // strength would let the words show through the shape.
    auto outgoing = song_frame(10);
    outgoing.objects = {green_shape("", 20)};
    auto incoming = outgoing;
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    pres.content_progress = 0.5;
    const QImage live = paint(pres, {}, false);
    const QImage cached = paint(pres, {}, true);
    CHECK(words_on_shape(live) == 0);
    CHECK(words_on_shape(cached) == 0);
    CHECK(seventhslide::test::mean_abs_diff(live, cached) < 1.5);
}

TEST_CASE("Each side of a moving change keeps its order, the outgoing too",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    Media media;
    const bool scratch = GENERATE(false, true);
    CAPTURE(scratch);

    // The outgoing picture has a shape under it (hidden) and one over it, so
    // its content is two runs around the media.
    auto outgoing = media_frame(SceneMode::Image, 10, "blue");
    outgoing.objects = {at_x(green_shape("under", 5), 20), at_x(green_shape("over", 20), 120)};
    auto incoming = media_frame(SceneMode::Image, 10, "red");
    incoming.objects = {at_x(green_shape("te10", 20), 120)};
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    pres.content_progress = 0.5;

    SECTION("push: the outgoing slide leaves to the left with its shape over it")
    {
        incoming.content_transition = {.type = seventhslide::model::TransitionType::Push,
                                       .duration_ms = 500};
        pres.content_type = seventhslide::model::TransitionType::Push;
        const QImage img = paint(pres, media.resources(), scratch);
        // Outgoing x 120..160 is now 20..60; its hidden shape (x 20..60) is
        // off the left edge.
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
        CHECK(nearly_rgb(img.pixel(80, k_spot_y), 0, 0, 255));
    }
    SECTION("reveal: the incoming slide, still, under the one going")
    {
        incoming.content_transition = {.type = seventhslide::model::TransitionType::Reveal,
                                       .duration_ms = 500};
        pres.content_type = seventhslide::model::TransitionType::Reveal;
        const QImage img = paint(pres, media.resources(), scratch);
        // The right half shows the incoming slide: its shape over its picture.
        CHECK(nearly_rgb(img.pixel(140, k_spot_y), 0, 255, 0));
        CHECK(nearly_rgb(img.pixel(180, k_spot_y), 255, 0, 0));
        // The left half is the outgoing slide, its shape over its picture.
        CHECK(nearly_rgb(img.pixel(k_spot_x, k_spot_y), 0, 255, 0));
    }
}

TEST_CASE("Title boxes keep their place among the objects through a dissolve",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();
    const bool scratch = GENERATE(false, true);
    CAPTURE(scratch);

    auto outgoing = song_frame(0);
    outgoing.text.reset();
    outgoing.overlay.boxes.push_back(title_box(10));
    auto incoming = outgoing;
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;
    for (const double progress : {0.5, 1.0}) {
        pres.content_progress = progress;
        INFO("progress " << progress);
        outgoing.objects = {green_shape("behind", 0)};
        incoming.objects = {green_shape("behind", 0)};
        CHECK(words_on_shape(paint(pres, {}, scratch)) > 20);
        outgoing.objects = {green_shape("front", 20)};
        incoming.objects = {green_shape("front", 20)};
        CHECK(words_on_shape(paint(pres, {}, scratch)) == 0);
    }
}

TEST_CASE("A change keeps at most a few pictures, and draws the rest anew",
          "[render][element_order]")
{
    seventhslide::test::ensure_qt_application();

    // Title boxes between shapes that change one by one: three runs a side,
    // six in all, more than a change keeps pictures of.
    auto outgoing = song_frame(0);
    outgoing.text.reset();
    outgoing.overlay.boxes = {title_box(1), title_box(3), title_box(5)};
    outgoing.overlay.boxes[0].y = -30;
    outgoing.overlay.boxes[2].y = 30;
    outgoing.objects = {at_x(green_shape("a", 2), 10), at_x(green_shape("b", 4), 140)};
    auto incoming = outgoing;
    incoming.objects[1].object->fill.color = "#ff00ff";
    ScenePresentation pres = ScenePresentation::still(incoming);
    pres.outgoing = &outgoing;

    SceneFadeLayers layers;
    for (const double progress : {0.25, 0.5, 0.75, 1.0}) {
        pres.content_progress = progress;
        INFO("progress " << progress);
        const QImage live = paint(pres, {}, false);
        const QImage cached = paint_with(pres, {}, layers);
        CHECK(layers.runs.size() == seventhslide::render::k_fade_run_pictures);
        CHECK(seventhslide::test::mean_abs_diff(live, cached) < 1.5);
    }
}
