#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pixel_compare.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/render/background_painter.hpp>
#include <seventhslide/render/cover_fit.hpp>
#include <seventhslide/render/static_assets.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <filesystem>
#include <utility>

using seventhslide::render::BackgroundImageCache;
using seventhslide::render::background_hides_stacks;
using seventhslide::render::canvas_background_covers_viewport;
using seventhslide::render::fit_canvas;
using seventhslide::render::cover_source;
using seventhslide::render::paint_background;
using seventhslide::render::render_scene_frame_image;
using seventhslide::render::resolve_static_asset;
using seventhslide::render::safe_static_url;
using seventhslide::scene::SceneBackground;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;

using seventhslide::test::nearly_rgb;

TEST_CASE("cover_source matches CSS background-size:cover crop", "[render]")
{
    SECTION("wider image crops horizontally")
    {
        // 200x100 into 100x100 -> scale=1, src 100x100 centered at x=50
        const auto c = cover_source(200, 100, 100, 100);
        REQUIRE(c.src_w == Catch::Approx(100.0));
        REQUIRE(c.src_h == Catch::Approx(100.0));
        REQUIRE(c.src_x == Catch::Approx(50.0));
        REQUIRE(c.src_y == Catch::Approx(0.0));
    }

    SECTION("taller image crops vertically")
    {
        const auto c = cover_source(100, 200, 100, 100);
        REQUIRE(c.src_w == Catch::Approx(100.0));
        REQUIRE(c.src_h == Catch::Approx(100.0));
        REQUIRE(c.src_x == Catch::Approx(0.0));
        REQUIRE(c.src_y == Catch::Approx(50.0));
    }

    SECTION("zero dims → empty")
    {
        const auto c = cover_source(0, 10, 100, 100);
        REQUIRE(c.src_w == Catch::Approx(0.0));
    }

    SECTION("a fractional destination keeps its own aspect")
    {
        // A freeform or media box need not land on whole pixels. The crop
        // must have the box's exact aspect, or the image is drawn stretched.
        const double dest_w = 100.9;
        const double dest_h = 50.0;
        const auto c = cover_source(1000, 1000, dest_w, dest_h);
        REQUIRE(c.src_w / c.src_h == Catch::Approx(dest_w / dest_h).epsilon(1e-12));
        REQUIRE(c.src_x == Catch::Approx(0.0));
        REQUIRE(c.src_y == Catch::Approx((1000 - c.src_h) * 0.5));
    }

    SECTION("a destination under one pixel still crops")
    {
        const auto c = cover_source(200, 100, 0.5, 0.5);
        REQUIRE(c.src_w == Catch::Approx(100.0));
        REQUIRE(c.src_h == Catch::Approx(100.0));
    }
}

TEST_CASE("safe_static_url rejects unsafe paths", "[render]")
{
    REQUIRE(safe_static_url("/static/theme_backgrounds/a.png")
            == std::string("/static/theme_backgrounds/a.png"));
    // Spaces / parentheses are common in imported video filenames.
    REQUIRE(safe_static_url("/static/videos/My Clip (1).webm")
            == std::string("/static/videos/My Clip (1).webm"));
    REQUIRE_FALSE(safe_static_url("https://evil.example/x.png").has_value());
    REQUIRE_FALSE(safe_static_url("/static/../etc/passwd").has_value());
    REQUIRE_FALSE(safe_static_url("/static/foo\"bar.png").has_value());
    REQUIRE_FALSE(safe_static_url("/media/bg.jpg").has_value());
}

TEST_CASE("paint_background color / transparent / image", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SECTION("color fills dest")
    {
        QImage img(80, 60, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Color;
        bg.color = "#ff0000";
        paint_background(p, bg, QRectF(0, 0, 80, 60));
        p.end();
        REQUIRE(nearly_rgb(img.pixel(40, 30), 255, 0, 0, 8));
    }

    SECTION("transparent leaves dest untouched")
    {
        QImage img(40, 40, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0, 0, 40));
        QPainter p(&img);
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Transparent;
        paint_background(p, bg, QRectF(0, 0, 40, 40));
        p.end();
        REQUIRE(nearly_rgb(img.pixel(10, 10), 0, 0, 40, 8));
    }

    SECTION("named and functional colors paint, junk stays black")
    {
        const auto painted = [](const char* color) {
            QImage img(20, 20, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            SceneBackground bg;
            bg.type = seventhslide::layout::BackgroundType::Color;
            bg.color = color;
            paint_background(p, bg, QRectF(0, 0, 20, 20));
            p.end();
            return img.pixel(10, 10);
        };
        REQUIRE(painted("red") == qRgb(255, 0, 0));
        REQUIRE(painted("rgb(0, 128, 0)") == qRgb(0, 128, 0));
        REQUIRE(painted("#abc") == QColor(QStringLiteral("#abc")).rgb());
        REQUIRE(painted("not-a-color") == qRgb(0, 0, 0));
    }

    SECTION("image cover paints from the media folder")
    {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const auto root = std::filesystem::path(tmp.path().toStdString());
        const auto dir = root / "theme_backgrounds";
        std::filesystem::create_directories(dir);

        QImage src(100, 50, QImage::Format_RGB32);
        src.fill(QColor(0, 200, 0));
        // Mark left half blue so a centered cover crop into a square keeps green
        // (full height) and still includes blue on the left of the crop.
        for (int y = 0; y < 50; ++y) {
            for (int x = 0; x < 25; ++x) {
                src.setPixel(x, y, qRgb(0, 0, 220));
            }
        }
        REQUIRE(src.save(QString::fromStdString((dir / "bg.png").string()), "PNG"));

        BackgroundImageCache cache;
        cache.set_static_root(root);
        REQUIRE(cache.ensure("/static/theme_backgrounds/bg.png"));

        QImage dest(50, 50, QImage::Format_ARGB32_Premultiplied);
        dest.fill(Qt::black);
        QPainter p(&dest);
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Image;
        bg.image = "/static/theme_backgrounds/bg.png";
        paint_background(p, bg, QRectF(0, 0, 50, 50), {.cache = &cache});
        p.end();

        // Cover crop of 100x50 into 50x50 -> src x=25..75 (all green in our image)
        REQUIRE(nearly_rgb(dest.pixel(25, 25), 0, 200, 0, 20));
    }

    SECTION("scaled ensure shrinks oversized sources")
    {
        QTemporaryDir tmp;
        REQUIRE(tmp.isValid());
        const auto root = std::filesystem::path(tmp.path().toStdString());
        const auto dir = root / "images";
        std::filesystem::create_directories(dir);

        QImage src(800, 600, QImage::Format_RGB32);
        src.fill(QColor(10, 20, 30));
        REQUIRE(src.save(QString::fromStdString((dir / "big.png").string()), "PNG"));

        BackgroundImageCache cache;
        cache.set_static_root(root);
        cache.set_max_edge(200);
        REQUIRE(cache.ensure("/static/images/big.png", 200));
        const QImage loaded = cache.get("/static/images/big.png", 200);
        REQUIRE_FALSE(loaded.isNull());
        REQUIRE(std::max(loaded.width(), loaded.height()) <= 200);
    }

    SECTION("missing image stays transparent")
    {
        BackgroundImageCache cache;
        cache.set_static_root("/nonexistent/static/root");
        QImage dest(40, 40, QImage::Format_ARGB32_Premultiplied);
        dest.fill(QColor(12, 12, 12));
        QPainter p(&dest);
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Image;
        bg.image = "/static/theme_backgrounds/missing.png";
        paint_background(p, bg, QRectF(0, 0, 40, 40), {.cache = &cache});
        p.end();
        REQUIRE(nearly_rgb(dest.pixel(5, 5), 12, 12, 12, 8));
    }

    SECTION("video uses cover frame; poster color without frame")
    {
        QImage dest(40, 40, QImage::Format_ARGB32_Premultiplied);
        dest.fill(Qt::black);
        QPainter p(&dest);
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Video;
        bg.color = "#004080";
        bg.video = "/static/theme_backgrounds/loop.mp4";
        paint_background(p, bg, QRectF(0, 0, 40, 40), {}, nullptr);
        p.end();
        REQUIRE(nearly_rgb(dest.pixel(5, 5), 0, 64, 128, 8));

        QImage frame(80, 40, QImage::Format_RGB32);
        frame.fill(QColor(0, 180, 0));
        dest.fill(Qt::black);
        QPainter p2(&dest);
        paint_background(p2, bg, QRectF(0, 0, 40, 40), {}, &frame);
        p2.end();
        REQUIRE(nearly_rgb(dest.pixel(20, 20), 0, 180, 0, 20));
    }
}

TEST_CASE("blank keeps background; resolve_static_asset", "[render]")
{
    seventhslide::test::ensure_qt_application();

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    std::filesystem::create_directories(root / "images");
    QImage src(16, 16, QImage::Format_RGB32);
    src.fill(QColor(30, 30, 200));
    REQUIRE(src.save(QString::fromStdString((root / "images" / "t.png").string()), "PNG"));

    REQUIRE(resolve_static_asset("/static/images/t.png", root).has_value());
    REQUIRE_FALSE(resolve_static_asset("/static/../images/t.png", root).has_value());

    BackgroundImageCache cache;
    cache.set_static_root(root);

    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = 64;
    frame.canvas_height = 48;
    frame.is_blank = true;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#203040";

    seventhslide::render::SceneResources res;
    res.cache = &cache;
    const QImage out = render_scene_frame_image(frame, 64, 48, Qt::black, res);
    REQUIRE(nearly_rgb(out.pixel(32, 24), 0x20, 0x30, 0x40, 12));
}

TEST_CASE("transparency checkerboard alternates and composites", "[render]")
{
    seventhslide::test::ensure_qt_application();

    using seventhslide::render::composite_on_transparency_checkerboard;
    using seventhslide::render::paint_transparency_checkerboard;
    using seventhslide::render::transparency_checkerboard_cell_px;

    REQUIRE(transparency_checkerboard_cell_px(160, 90) == 8);
    REQUIRE(transparency_checkerboard_cell_px(1920, 1080) == 24);

    QImage grid(32, 32, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&grid);
        paint_transparency_checkerboard(p, QRectF(0, 0, 32, 32), 8);
    }
    const QRgb a = grid.pixel(0, 0);
    const QRgb b = grid.pixel(8, 0);
    REQUIRE(a != b);
    REQUIRE(grid.pixel(0, 8) == b);
    REQUIRE(grid.pixel(8, 8) == a);

    QImage overlay(32, 32, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    {
        QPainter p(&overlay);
        p.fillRect(0, 0, 8, 8, QColor(255, 0, 0));
    }
    const QImage composed = composite_on_transparency_checkerboard(overlay);
    REQUIRE(nearly_rgb(composed.pixel(2, 2), 255, 0, 0, 4));
    REQUIRE(composed.pixel(16, 16) == grid.pixel(16, 16));
}

TEST_CASE("A covering opaque background paints once, with identical pixels", "[render]")
{
    seventhslide::test::ensure_qt_application();

    // A frame image with structure, so a misplaced or rescaled draw would show.
    QImage video(160, 90, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < video.height(); ++y) {
        for (int x = 0; x < video.width(); ++x) {
            video.setPixel(x, y, qRgb((x * 7) % 256, (y * 13) % 256, (x * y) % 256));
        }
    }

    // What render_scene_frame_image did before: the viewport pass, then the
    // canvas pass over it.
    const auto two_passes = [](const SceneFrame& frame, const QImage* bg_video) {
        QImage img(frame.canvas_width, frame.canvas_height, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF rect(0, 0, frame.canvas_width, frame.canvas_height);
        paint_background(painter, frame.background, rect, {}, bg_video);
        paint_background(painter, frame.background, rect, {}, bg_video);
        painter.end();
        return img;
    };

    SceneFrame frame;
    frame.mode = SceneMode::Empty;
    frame.canvas_width = 320;
    frame.canvas_height = 180;
    const auto exact = fit_canvas(320, 180, 320, 180);

    SECTION("video frame")
    {
        frame.background.type = seventhslide::layout::BackgroundType::Video;
        REQUIRE(canvas_background_covers_viewport(frame.background, exact, &video));
        REQUIRE(background_hides_stacks(frame.background, exact, &video));
        seventhslide::render::SceneResources res;
        res.background_video_frame = &video;
        const QImage once = render_scene_frame_image(frame, 320, 180, QColor(Qt::black), res);
        REQUIRE(once == two_passes(frame, &video));
    }

    SECTION("opaque colour")
    {
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#336699";
        REQUIRE(canvas_background_covers_viewport(frame.background, exact, nullptr));
        REQUIRE(background_hides_stacks(frame.background, exact, nullptr));
        const QImage once = render_scene_frame_image(frame, 320, 180);
        REQUIRE(once == two_passes(frame, nullptr));
    }

    SECTION("translucent colour, images and letterboxed canvases keep both passes")
    {
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#80336699";
        REQUIRE_FALSE(canvas_background_covers_viewport(frame.background, exact, nullptr));
        REQUIRE_FALSE(background_hides_stacks(frame.background, exact, nullptr));
        frame.background.type = seventhslide::layout::BackgroundType::Image;
        REQUIRE_FALSE(canvas_background_covers_viewport(frame.background, exact, nullptr));
        REQUIRE_FALSE(background_hides_stacks(frame.background, exact, nullptr));
        frame.background.type = seventhslide::layout::BackgroundType::Video;
        const auto letterboxed = fit_canvas(320, 180, 400, 180);
        REQUIRE_FALSE(canvas_background_covers_viewport(frame.background, letterboxed, &video));
        REQUIRE_FALSE(background_hides_stacks(frame.background, letterboxed, &video));
    }
}

TEST_CASE("A no-op top background leaves the stacks underneath visible", "[render][stacks]")
{
    seventhslide::test::ensure_qt_application();

    // Green is the unpainted letterbox. A culled stack leaves it behind, so a
    // red lower stack cannot pass by accident.
    const QColor letterbox(0, 255, 0);
    const auto exact = fit_canvas(320, 180, 320, 180);
    const auto wide = fit_canvas(320, 180, 400, 180);

    const auto lower_red = [] {
        SceneFrame lower;
        lower.mode = SceneMode::Image;
        lower.canvas_width = 320;
        lower.canvas_height = 180;
        lower.background.type = seventhslide::layout::BackgroundType::Color;
        lower.background.color = "#ff0000";
        return lower;
    };

    const auto with_lower = [&](SceneFrame top) {
        top.canvas_width = 320;
        top.canvas_height = 180;
        top.stacked_below.push_back(seventhslide::scene::share_frame(lower_red()));
        return top;
    };

    const auto center_px = [](const QImage& img) {
        return img.pixel(img.width() / 2, img.height() / 2);
    };

    SECTION("transparent on an exact fit skips the viewport pass and shows the stack")
    {
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Transparent;
        REQUIRE(canvas_background_covers_viewport(bg, exact, nullptr));
        REQUIRE_FALSE(background_hides_stacks(bg, exact, nullptr));

        SceneFrame top;
        top.mode = SceneMode::Song;
        top.background.type = seventhslide::layout::BackgroundType::Transparent;
        const QImage img = render_scene_frame_image(with_lower(top), 320, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(255, 0, 0));
    }

    SECTION("the same stack shows when the output is letterboxed")
    {
        SceneFrame top;
        top.mode = SceneMode::Song;
        top.background.type = seventhslide::layout::BackgroundType::Transparent;
        REQUIRE_FALSE(background_hides_stacks(top.background, wide, nullptr));
        const QImage img = render_scene_frame_image(with_lower(std::move(top)), 400, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(255, 0, 0));
    }

    SECTION("an opaque top fill still covers the stack")
    {
        SceneFrame top;
        top.mode = SceneMode::Song;
        top.background.type = seventhslide::layout::BackgroundType::Color;
        top.background.color = "#000000";
        REQUIRE(background_hides_stacks(top.background, exact, nullptr));
        const QImage img = render_scene_frame_image(with_lower(std::move(top)), 320, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(0, 0, 0));
    }

    SECTION("a layered image whose background was cleared does not wipe the image under it")
    {
        // clear_media_wipe_if_layered does this to every image/video layer
        // above the bottom, so the upper frame will not paint over the stack.
        SceneFrame top;
        top.mode = SceneMode::Image;
        top.background.type = seventhslide::layout::BackgroundType::Transparent;
        top.background.color.clear();
        top.background.image.clear();
        top.background.video.clear();
        REQUIRE_FALSE(background_hides_stacks(top.background, exact, nullptr));
        const QImage img = render_scene_frame_image(with_lower(std::move(top)), 320, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(255, 0, 0));
    }

    SECTION("a video with no frame and no poster shows the stack")
    {
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Video;
        bg.color.clear();
        REQUIRE(canvas_background_covers_viewport(bg, exact, nullptr));
        REQUIRE_FALSE(background_hides_stacks(bg, exact, nullptr));

        SceneFrame top;
        top.mode = SceneMode::Song;
        top.background = bg;
        const QImage img = render_scene_frame_image(with_lower(std::move(top)), 320, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(255, 0, 0));
    }

    SECTION("a video with no frame and an opaque poster still covers the stack")
    {
        SceneBackground bg;
        bg.type = seventhslide::layout::BackgroundType::Video;
        bg.color = "#0000ff";
        REQUIRE(canvas_background_covers_viewport(bg, exact, nullptr));
        REQUIRE(background_hides_stacks(bg, exact, nullptr));

        SceneFrame top;
        top.mode = SceneMode::Song;
        top.background = bg;
        const QImage img = render_scene_frame_image(with_lower(std::move(top)), 320, 180, letterbox);
        REQUIRE(center_px(img) == qRgb(0, 0, 255));
    }
}
