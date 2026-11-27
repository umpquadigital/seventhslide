// Static elements are drawn once and copied while unchanged.

#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "test_qt_app.hpp"
#include "test_temp_dir.hpp"

#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/canvas_fit.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/text_metrics.hpp>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <tuple>

namespace sc = seventhslide::scene;
using seventhslide::render::ElementRasterCache;
using seventhslide::render::SceneResources;
using seventhslide::render::ScenePresentation;

namespace {

QImage pattern(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixel(x, y, qRgb((x * 7) & 255, (y * 3) & 255, (x ^ y) & 255));
        }
    }
    return img;
}

/// Everything static a slide can show: a scaled picture background, big
/// lyrics, a title, a rotated outlined star, a flipped photo and a clock,
/// plus a stack underneath. (Effects are translucent paint over the body,
/// which rounds inside a raster: they have a test of their own.)
sc::SceneFrame rich_frame(seventhslide::layout::ITextMeasurer& measurer)
{
    sc::SceneFrame frame;
    frame.mode = sc::SceneMode::Song;
    frame.background.type = seventhslide::layout::BackgroundType::Image;
    frame.background.image = "/static/images/bg.png";

    sc::SceneTextContent text;
    text.box = {110, 170, 1700, 680, "Liberation Sans", 84};
    text.box.opacity = 0.9;
    for (const char* line : {"A mighty <b>fortress</b> is our God,", "a bulwark never failing"}) {
        sc::SceneTextLine l;
        l.runs = seventhslide::model::parse_inline_markup(line);
        text.lines.push_back(l);
    }
    sc::measure_text_content(text, measurer);
    frame.text = text;

    seventhslide::layout::OverlayBox title;
    title.x = 160;
    title.y = 20;
    title.w = 1600;
    title.h = 130;
    title.font_family = "Liberation Sans";
    title.font_size = 96;
    title.lines.push_back(seventhslide::model::parse_inline_markup("A Mighty Fortress"));
    title.layout = seventhslide::layout::lay_out_overlay(title, measurer);
    frame.overlay.boxes.push_back(title);

    sc::SceneObject star;
    star.z = 1;
    star.object.emplace();
    star.object->type = seventhslide::canvas::ObjectType::Shape;
    star.object->shape_kind = seventhslide::geom::ShapeKind::Star;
    star.object->x = 1500;
    star.object->y = 740;
    star.object->w = 300;
    star.object->h = 300;
    star.object->rotation = 17;
    star.object->fill.color = "#c81e3c";
    star.object->line.color = "#ffffff";
    star.object->line.width = 9;
    frame.objects.push_back(star);

    sc::SceneObject photo;
    photo.z = 2;
    photo.object.emplace();
    photo.object->type = seventhslide::canvas::ObjectType::Image;
    photo.object->filename = "photo.png";
    photo.object->x = 60.5;
    photo.object->y = 760.25;
    photo.object->w = 450;
    photo.object->h = 300;
    photo.object->rotation = -8;
    photo.object->flip_h = true;
    frame.objects.push_back(photo);

    sc::SceneObject clock;
    clock.z = 3;
    clock.object.emplace();
    clock.object->type = seventhslide::canvas::ObjectType::Clock;
    clock.object->x = 1600;
    clock.object->y = 960;
    clock.object->w = 300;
    clock.object->h = 100;
    clock.object->font_family = "Liberation Sans";
    clock.object->font_size = 72;
    clock.object->clock_seconds = true;
    frame.objects.push_back(clock);

    sc::SceneFrame below;
    below.background.type = seventhslide::layout::BackgroundType::Color;
    below.background.color = "#203040";
    sc::SceneObject panel;
    panel.object.emplace();
    panel.object->type = seventhslide::canvas::ObjectType::Shape;
    panel.object->shape_kind = seventhslide::geom::ShapeKind::RoundedRect;
    panel.object->x = 100;
    panel.object->y = 880;
    panel.object->w = 1720;
    panel.object->h = 180;
    panel.object->fill.color = "rgba(0,0,0,0.6)";
    below.objects.push_back(panel);
    frame.stacked_below.push_back(sc::share_frame(below));
    return frame;
}

struct Fixture {
    Fixture()
    {
        std::filesystem::create_directories(root.path / "images");
        REQUIRE(pattern(1280, 720).save(
            QString::fromStdString((root.path / "images" / "bg.png").string())));
        REQUIRE(pattern(1600, 1067).save(
            QString::fromStdString((root.path / "images" / "photo.png").string())));
        images.set_static_root(root.path);
        res.cache = &images;
        res.faces = &measurer.faces();
        res.now = QDateTime(QDate(2026, 10, 5), QTime(10, 30, 15));
        frame = rich_frame(measurer);
    }

    [[nodiscard]] QImage composite(int w, int h, ElementRasterCache* rasters)
    {
        SceneResources with = res;
        with.rasters = rasters;
        return seventhslide::render::render_scene_presentation_image(
            ScenePresentation::still(frame), w, h, QColor(Qt::black), with);
    }

    /// First member: the measurer below needs the application to exist.
    struct QtApp {
        QtApp() { seventhslide::test::ensure_qt_application(); }
    } app;
    const seventhslide::test::TempDir root;
    struct ShippedFonts : seventhslide::fonts::FontRegistry {
        ShippedFonts() { seventhslide::test::use_shipped_fonts(*this); }
    } fonts;
    seventhslide::layout::QtTextMeasurer measurer{fonts};
    seventhslide::render::BackgroundImageCache images;
    SceneResources res;
    sc::SceneFrame frame;
};

}  // namespace

namespace {

/// The largest difference in any channel of any pixel.
int max_channel_delta(const QImage& a, const QImage& b)
{
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                              std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
        }
    }
    return worst;
}

}  // namespace

TEST_CASE("A retained composite is pixel-identical to a live one", "[render][retained]")
{
    Fixture f;
    // Exact, fractional and letterboxed fits.
    for (const auto& [w, h] : {std::pair{1920, 1080}, {1366, 768}, {1000, 777}}) {
        INFO(w << "x" << h);
        ElementRasterCache rasters;
        const QImage live = f.composite(w, h, nullptr);
        const QImage first = f.composite(w, h, &rasters);   // every element drawn into a raster
        const QImage second = f.composite(w, h, &rasters);  // every element copied
        REQUIRE(rasters.misses() > 0);
        REQUIRE(rasters.hits() > 0);
        REQUIRE(first == live);
        REQUIRE(second == live);
    }
}

// An element that lays translucent paint over its own translucent paint (a
// see-through star's stroke over its fill) is blended inside its raster before
// it meets the slide, so that overlap rounds to 8 bits once more than drawing
// straight onto the slide. The same paint covers the same pixels; the copy is
// at most a couple of levels off there, and exact everywhere else.
TEST_CASE("Translucent paint over itself rounds within two levels", "[render][retained]")
{
    Fixture f;
    f.frame.objects[0].object->opacity = 0.8;
    ElementRasterCache rasters;
    const QImage live = f.composite(1920, 1080, nullptr);
    const QImage retained = f.composite(1920, 1080, &rasters);
    REQUIRE(max_channel_delta(live, retained) <= 2);
    // Only inside the star.
    const QRect star(1440, 680, 420, 420);
    for (int y = 0; y < live.height(); y += 3) {
        for (int x = 0; x < live.width(); x += 3) {
            if (!star.contains(x, y)) {
                REQUIRE(live.pixel(x, y) == retained.pixel(x, y));
            }
        }
    }
}

// An effect is translucent paint under or over its body, blended inside the
// raster before the raster meets the slide: like any translucent overlap, a
// couple of levels off where the two meet, exact elsewhere.
TEST_CASE("Effects retain within two levels", "[render][retained][effects]")
{
    Fixture f;
    auto& lyrics = f.frame.text->box;
    lyrics.effects.text_outline.color = "#000000";
    lyrics.effects.text_outline.width = 3;
    lyrics.effects.text_shadow.on = true;
    f.frame.overlay.boxes[0].effects.text_shadow.on = true;
    f.frame.overlay.boxes[0].effects.text_shadow.blur = 6;
    auto& photo = *f.frame.objects[1].object;
    photo.effects.shadow.on = true;
    photo.effects.shadow.blur = 10;
    photo.effects.shadow.distance = 8;
    photo.effects.soft_edge = 6;
    auto& clock = *f.frame.objects[2].object;
    clock.effects.glow.on = true;
    clock.effects.glow.size = 8;
    clock.fill.color = "#40000000";
    auto& star = *f.frame.objects[0].object;
    star.effects.shadow.on = true;

    for (const auto& [w, h] : {std::pair{1920, 1080}, {1366, 768}}) {
        INFO(w << "x" << h);
        ElementRasterCache rasters;
        const QImage live = f.composite(w, h, nullptr);
        const QImage first = f.composite(w, h, &rasters);
        const QImage second = f.composite(w, h, &rasters);
        REQUIRE(rasters.hits() > 0);
        CHECK(max_channel_delta(live, first) <= 2);
        CHECK(first == second);
    }
}

TEST_CASE("A retained composite matches on a high-DPI target", "[render][retained]")
{
    Fixture f;
    const auto paint = [&](ElementRasterCache* rasters) {
        QImage target(1600, 900, QImage::Format_ARGB32_Premultiplied);
        target.setDevicePixelRatio(2.0);
        target.fill(Qt::black);
        SceneResources with = f.res;
        with.rasters = rasters;
        QPainter painter(&target);
        const auto fit = seventhslide::render::fit_canvas(1920, 1080, 800, 450);
        seventhslide::render::paint_scene_presentation(
            painter, ScenePresentation::still(f.frame), fit, QRectF(0, 0, 800, 450), with);
        painter.end();
        return target;
    };
    ElementRasterCache rasters;
    const QImage live = paint(nullptr);
    REQUIRE(paint(&rasters) == live);
    REQUIRE(paint(&rasters) == live);
    REQUIRE(rasters.hits() > 0);
}

TEST_CASE("Unchanged elements are copied and an edited one is drawn again", "[render][retained]")
{
    Fixture f;
    ElementRasterCache rasters;
    (void)f.composite(1920, 1080, &rasters);
    const auto drawn = rasters.misses();
    // At an exact fit the background's viewport and canvas passes are the same
    // rectangle: the second is already a copy.
    const auto draws = drawn + rasters.hits();
    REQUIRE(drawn >= 6);

    (void)f.composite(1920, 1080, &rasters);
    REQUIRE(rasters.misses() == drawn);
    REQUIRE(rasters.hits() == 2 * draws - drawn);

    // A new title: that box alone is drawn again.
    f.frame.overlay.boxes[0].lines[0] = seventhslide::model::parse_inline_markup("Be Thou My Vision");
    f.frame.overlay.boxes[0].layout =
        seventhslide::layout::lay_out_overlay(f.frame.overlay.boxes[0], f.measurer);
    (void)f.composite(1920, 1080, &rasters);
    REQUIRE(rasters.misses() == drawn + 1);

    // So is everything at a new size; the old rasters are left to age out.
    const auto before = rasters.misses();
    (void)f.composite(1280, 720, &rasters);
    REQUIRE(rasters.misses() - before >= drawn);
}

TEST_CASE("Each clock second is its own raster", "[render][retained]")
{
    Fixture f;
    ElementRasterCache rasters;
    (void)f.composite(1920, 1080, &rasters);
    const auto drawn = rasters.misses();
    // The same second, composited again for a video frame: a copy.
    (void)f.composite(1920, 1080, &rasters);
    REQUIRE(rasters.misses() == drawn);
    // The next second: only the clock is drawn.
    f.res.now = f.res.now.addSecs(1);
    const QImage live = f.composite(1920, 1080, nullptr);
    REQUIRE(f.composite(1920, 1080, &rasters) == live);
    REQUIRE(rasters.misses() == drawn + 1);
}

TEST_CASE("Translucent painting draws live", "[render][retained]")
{
    Fixture f;
    ElementRasterCache rasters;
    QImage target(400, 300, QImage::Format_ARGB32_Premultiplied);
    target.fill(Qt::black);
    QPainter painter(&target);
    painter.setOpacity(0.5);  // a cross-fade without fade layers
    seventhslide::render::paint_text_content(painter, *f.frame.text,
                                             {.faces = &f.measurer.faces(), .rasters = &rasters});
    painter.end();
    REQUIRE(rasters.size() == 0);
    REQUIRE(rasters.misses() == 0);
}

TEST_CASE("Rasters no longer drawn age out, within a byte budget", "[render][retained]")
{
    Fixture f;
    ElementRasterCache rasters;
    (void)f.composite(1920, 1080, &rasters);
    REQUIRE(rasters.size() > 0);
    REQUIRE(rasters.bytes() > 0);
    for (std::uint64_t i = 0; i <= ElementRasterCache::k_keep_passes; ++i) {
        rasters.begin_pass();
    }
    REQUIRE(rasters.size() == 0);
    REQUIRE(rasters.bytes() == 0);

    // A budget smaller than the slide keeps only the most recent rasters.
    ElementRasterCache small(4u * 1024u * 1024u);
    (void)f.composite(1920, 1080, &small);
    REQUIRE(small.size() >= 1);
    REQUIRE((small.size() == 1 || small.bytes() <= 4u * 1024u * 1024u));
}

namespace {

/// A picture with see-through parts, for a background that lets what is
/// under it show.
QImage translucent_pattern(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.setPixel(x, y, qRgba((x * 5) & 255, (y * 11) & 255, (x + y) & 255, (x * 3 + y) & 255));
        }
    }
    return img;
}

/// ``f``'s slide with nothing on it but its background picture, ``image``.
sc::SceneFrame background_only(const Fixture& f, const char* image)
{
    sc::SceneFrame frame = f.frame;
    frame.text.reset();
    frame.overlay.boxes.clear();
    frame.objects.clear();
    frame.stacked_below.clear();
    frame.background.image = image;
    return frame;
}

/// ``from``'s background changing into ``to``'s, ``progress`` of the way
/// (``to`` settled, without ``from``), painted onto ``target`` (filled with
/// ``under`` first).
void paint_background_change(QImage& target,
                             const QColor& under,
                             const sc::SceneFrame* from,
                             const sc::SceneFrame& to,
                             double progress,
                             const SceneResources& res)
{
    auto pres = ScenePresentation::still(to);
    if (from != nullptr) {
        pres.background = &to.background;
        pres.background_outgoing = &from->background;
        pres.background_progress = progress;
    }
    target.fill(under);
    QPainter painter(&target);
    const QSizeF logical = target.deviceIndependentSize();
    const auto fit = seventhslide::render::fit_canvas(1920, 1080, static_cast<int>(logical.width()),
                                                      static_cast<int>(logical.height()));
    seventhslide::render::paint_scene_presentation(painter, pres, fit,
                                                   QRectF(QPointF(0, 0), logical), res);
    painter.end();
}

}  // namespace

// On whole pixels a fading background picture is its raster copied at the
// painter's opacity, pixel for pixel: each side of a change into another
// picture is copied from its raster on every frame, not scaled again.
TEST_CASE("A fading background picture is copied from its raster, pixel for pixel",
          "[render][retained][transition]")
{
    Fixture f;
    REQUIRE(translucent_pattern(1024, 1024).save(
        QString::fromStdString((f.root.path / "images" / "bg2.png").string())));
    const sc::SceneFrame from = background_only(f, "/static/images/bg.png");
    const sc::SceneFrame to = background_only(f, "/static/images/bg2.png");
    // Exact fits, and one at twice the pixels.
    for (const auto& [w, h, dpr] : {std::tuple{1920, 1080, 1.0}, {640, 360, 1.0}, {800, 450, 2.0}}) {
        INFO(w << "x" << h << " at " << dpr);
        QImage live(static_cast<int>(w * dpr), static_cast<int>(h * dpr),
                    QImage::Format_ARGB32_Premultiplied);
        live.setDevicePixelRatio(dpr);
        QImage retained = live;
        ElementRasterCache rasters;
        SceneResources with = f.res;
        with.rasters = &rasters;
        // Each settled first, as it was shown.
        paint_background_change(retained, Qt::black, nullptr, from, 1.0, with);
        paint_background_change(retained, Qt::black, nullptr, to, 1.0, with);
        const auto misses = rasters.misses();
        const auto hits = rasters.hits();
        for (int i = 1; i < 16; ++i) {
            const double progress = i / 16.0;
            INFO("progress " << progress);
            paint_background_change(live, Qt::black, &from, to, progress, f.res);
            paint_background_change(retained, Qt::black, &from, to, progress, with);
            REQUIRE(retained == live);
        }
        // Each side's two passes, every frame, copied.
        CHECK(rasters.misses() == misses);
        CHECK(rasters.hits() - hits == 15u * 4u);
    }
}

TEST_CASE("Off whole pixels a fading background picture is drawn live",
          "[render][retained][transition]")
{
    Fixture f;
    REQUIRE(translucent_pattern(1024, 1024).save(
        QString::fromStdString((f.root.path / "images" / "bg2.png").string())));
    const sc::SceneFrame from = background_only(f, "/static/images/bg.png");
    const sc::SceneFrame to = background_only(f, "/static/images/bg2.png");
    // A fit that leaves the canvas on part pixels, and a letterboxed one; the
    // viewport's own pass is still on whole pixels, and copied.
    for (const auto& [w, h] : {std::pair{1366, 768}, {1000, 777}}) {
        INFO(w << "x" << h);
        QImage live(w, h, QImage::Format_ARGB32_Premultiplied);
        QImage retained = live;
        ElementRasterCache rasters;
        SceneResources with = f.res;
        with.rasters = &rasters;
        for (int i = 1; i < 16; ++i) {
            const double progress = i / 16.0;
            INFO("progress " << progress);
            paint_background_change(live, Qt::transparent, &from, to, progress, f.res);
            paint_background_change(retained, Qt::transparent, &from, to, progress, with);
            REQUIRE(retained == live);
        }
        CHECK(rasters.hits() > 0);
    }
}

TEST_CASE("A picture copied at an opacity matches it drawn at that opacity",
          "[render][retained][transition]")
{
    Fixture f;
    REQUIRE(translucent_pattern(700, 1500).save(
        QString::fromStdString((f.root.path / "images" / "tall.png").string())));
    // Scaled down, up, and not at all; at whole-pixel offsets.
    struct Place {
        double scale;
        QPointF offset;
        QRectF dest;
    };
    const Place places[] = {{0.1, {3, 0}, {0, 0, 1920, 1080}},
                            {1.0 / 3.0, {0, 0}, {0, 0, 1920, 1080}},
                            {1.0, {7, 5}, {0, 0, 333, 187}},
                            {2.0, {0, 0}, {0, 0, 160, 90}}};
    for (const char* image : {"/static/images/bg.png", "/static/images/tall.png"}) {
        sc::SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Image;
        bg.image = image;
        for (const auto& place : places) {
            INFO(image << " scaled " << place.scale);
            const QRectF covered = QTransform::fromTranslate(place.offset.x(), place.offset.y())
                                       .scale(place.scale, place.scale)
                                       .mapRect(place.dest);
            const QImage under =
                translucent_pattern(static_cast<int>(std::ceil(covered.right())) + 2,
                                    static_cast<int>(std::ceil(covered.bottom())) + 2)
                    .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            ElementRasterCache rasters;
            for (int k = 1; k < 256; ++k) {
                INFO("opacity " << k << "/256");
                QImage got[2] = {under, under};
                for (int retained = 0; retained < 2; ++retained) {
                    seventhslide::render::PaintContext ctx;
                    ctx.cache = &f.images;
                    ctx.rasters = retained == 1 ? &rasters : nullptr;
                    QPainter painter(&got[retained]);
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                    painter.translate(place.offset);
                    painter.scale(place.scale, place.scale);
                    painter.setOpacity(k / 256.0);
                    seventhslide::render::paint_background(painter, bg, place.dest, ctx);
                    painter.end();
                }
                REQUIRE(got[1] == got[0]);
            }
            CHECK(rasters.misses() == 1);
            CHECK(rasters.hits() == 254);
        }
    }
}

TEST_CASE("A fading picture off whole pixels, clipped or turned is drawn live",
          "[render][retained][transition]")
{
    Fixture f;
    sc::SceneBackground bg;
    bg.type = seventhslide::layout::BackgroundType::Image;
    bg.image = "/static/images/bg.png";
    ElementRasterCache rasters;
    const auto paint = [&](const std::function<void(QPainter&)>& place) {
        QImage target(400, 300, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::black);
        QPainter painter(&target);
        place(painter);
        painter.setOpacity(0.5);
        seventhslide::render::paint_background(painter, bg, QRectF(0, 0, 320, 180),
                                               {.cache = &f.images, .rasters = &rasters});
        painter.end();
    };
    paint([](QPainter& p) { p.translate(0.5, 0); });
    paint([](QPainter& p) { p.scale(0.7111, 0.7111); });
    paint([](QPainter& p) { p.setClipRect(QRect(10, 10, 100, 100)); });
    paint([](QPainter& p) { p.rotate(90); p.translate(0, -300); });
    REQUIRE(rasters.size() == 0);
    REQUIRE(rasters.misses() == 0);
}
