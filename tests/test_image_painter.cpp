#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "pixel_compare.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/object_fit.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/render/video_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using seventhslide::model::static_media_url;
using seventhslide::render::BackgroundImageCache;
using seventhslide::render::image_area_rect;
using seventhslide::render::object_fit;
using seventhslide::render::paint_image_media;
using seventhslide::render::render_scene_frame_image;
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneImageMedia;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneObject;

using seventhslide::test::nearly_rgb;

namespace {

/// What a still paints with: ``cache`` and nothing else.
seventhslide::render::SceneResources with_cache(BackgroundImageCache& cache)
{
    seventhslide::render::SceneResources res;
    res.cache = &cache;
    return res;
}

seventhslide::canvas::Object shape_box(double x, double y, double w, double h, const char* fill)
{
    seventhslide::canvas::Object sh;
    sh.type = seventhslide::canvas::ObjectType::Shape;
    sh.x = x;
    sh.y = y;
    sh.w = w;
    sh.h = h;
    sh.fill.color = fill;
    return sh;
}

/// ``url`` is what the painter resolves ``file`` to; the test states it so a
/// change to the static URL scheme is seen here.
seventhslide::canvas::Object image_box(double x, double y, double w, double h, const char* file,
                                       const char* url, const char* fit)
{
    seventhslide::canvas::Object img;
    img.type = seventhslide::canvas::ObjectType::Image;
    img.x = x;
    img.y = y;
    img.w = w;
    img.h = h;
    img.filename = file;
    img.object_fit = fit;
    REQUIRE(seventhslide::canvas::object_media_url(img) == std::string(url));
    return img;
}



} // namespace

TEST_CASE("static_media_url builds /static paths", "[model]")
{
    REQUIRE(static_media_url("images", "a.png") == std::string("/static/images/a.png"));
    REQUIRE(static_media_url("images", "dir/a.png") == std::string("/static/images/a.png"));
    REQUIRE_FALSE(static_media_url("images", "").has_value());
    REQUIRE_FALSE(static_media_url("images", "..").has_value());
    REQUIRE_FALSE(static_media_url("", "a.png").has_value());
    // Stored names may use either separator.
    REQUIRE(static_media_url("images", "C:\\media\\b.png") == std::string("/static/images/b.png"));
    REQUIRE_FALSE(static_media_url("images", "dir/").has_value());
}

TEST_CASE("object_fit contain / cover / fill", "[render]")
{
    SECTION("contain letterboxes wide image")
    {
        // 200x100 into 100x100 -> scale 0.5 -> dest 100x50 centered y=25
        const auto p = object_fit("contain", 200, 100, 0, 0, 100, 100);
        REQUIRE(p.dst_w == Catch::Approx(100.0));
        REQUIRE(p.dst_h == Catch::Approx(50.0));
        REQUIRE(p.dst_x == Catch::Approx(0.0));
        REQUIRE(p.dst_y == Catch::Approx(25.0));
        REQUIRE(p.src_w == Catch::Approx(200.0));
    }

    SECTION("cover crops wide image")
    {
        const auto p = object_fit("cover", 200, 100, 10, 20, 100, 100);
        REQUIRE(p.dst_x == Catch::Approx(10.0));
        REQUIRE(p.dst_y == Catch::Approx(20.0));
        REQUIRE(p.dst_w == Catch::Approx(100.0));
        REQUIRE(p.src_w == Catch::Approx(100.0));
        REQUIRE(p.src_x == Catch::Approx(50.0));
    }

    SECTION("cover into a fractional box crops to the box's aspect")
    {
        const auto p = object_fit("cover", 1000, 1000, 3.25, 7.5, 100.9, 50.0);
        REQUIRE(p.src_w / p.src_h == Catch::Approx(100.9 / 50.0).epsilon(1e-12));
        REQUIRE(p.dst_w == Catch::Approx(100.9));
        REQUIRE(p.dst_h == Catch::Approx(50.0));
    }

    SECTION("fill stretches")
    {
        const auto p = object_fit("fill", 50, 25, 0, 0, 100, 100);
        REQUIRE(p.src_w == Catch::Approx(50.0));
        REQUIRE(p.dst_w == Catch::Approx(100.0));
        REQUIRE(p.dst_h == Catch::Approx(100.0));
    }
}

TEST_CASE("image_area_rect zero size fills canvas", "[render]")
{
    SceneFrame frame;
    frame.canvas_width = 1920;
    frame.canvas_height = 1080;
    SceneImageMedia media;
    media.area = {.enabled = true, .x = 10, .y = 20, .width = 0, .height = 0};
    frame.image = media;
    const QRectF r = image_area_rect(frame);
    REQUIRE(r.x() == Catch::Approx(10.0));
    REQUIRE(r.y() == Catch::Approx(20.0));
    REQUIRE(r.width() == Catch::Approx(1920.0));
    REQUIRE(r.height() == Catch::Approx(1080.0));
}

TEST_CASE("paint_image_media contain into area", "[render]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    const auto images = root / "images";
    std::filesystem::create_directories(images);

    // Solid red 40x20 source
    QImage src(40, 20, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(255, 0, 0));
    REQUIRE(src.save(QString::fromStdString((images / "red.png").string())));

    BackgroundImageCache cache;
    cache.set_static_root(root);

    SceneFrame frame;
    frame.mode = SceneMode::Image;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneImageMedia media;
    media.url = "/static/images/red.png";
    media.index = 0;
    media.count = 1;
    media.area = {.enabled = true, .x = 0, .y = 0, .width = 100, .height = 100};
    media.fit = "contain";
    frame.image = media;

    QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);
    out.fill(QColor(0, 255, 0));  // green underlay must not show through letterbox
    QPainter p(&out);
    paint_image_media(p, frame, {.cache = &cache});
    p.end();

    // contain into 100x100: dest is 100x50 at y=25 - center of area should be red
    REQUIRE(nearly_rgb(out.pixel(50, 50), 255, 0, 0));
    // Letterbox bands inside area are black (not green underlay / theme bleed)
    REQUIRE(nearly_rgb(out.pixel(50, 10), 0, 0, 0));
    REQUIRE(nearly_rgb(out.pixel(50, 90), 0, 0, 0));
    // Outside area stays green (plate is clipped to image area)
    REQUIRE(nearly_rgb(out.pixel(150, 50), 0, 255, 0));

    SECTION("disabled skips paint")
    {
        frame.image->area.enabled = false;
        out.fill(Qt::black);
        QPainter p2(&out);
        paint_image_media(p2, frame, {.cache = &cache});
        p2.end();
        REQUIRE(nearly_rgb(out.pixel(50, 50), 0, 0, 0));
    }

    SECTION("render_scene_frame_image includes image layer")
    {
        frame.image->area.enabled = true;
        const QImage rendered = render_scene_frame_image(frame, 200, 100, Qt::black, with_cache(cache));
        REQUIRE(nearly_rgb(rendered.pixel(50, 50), 255, 0, 0));
    }
}

TEST_CASE("paint_scene_objects fills shapes and images", "[render][freeform]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    const auto images = root / "images";
    std::filesystem::create_directories(images);

    QImage src(20, 20, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(0, 255, 0));
    REQUIRE(src.save(QString::fromStdString((images / "green.png").string())));

    BackgroundImageCache cache;
    cache.set_static_root(root);

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";

    SceneObject shape;
    shape.z = 0;
    shape.object = shape_box(10, 10, 40, 40, "#ff0000");
    SceneObject image;
    image.z = 1;
    image.object = image_box(80, 10, 40, 40, "green.png", "/static/images/green.png", "fill");
    frame.objects = {std::move(shape), std::move(image)};
    REQUIRE(cache.ensure(*seventhslide::canvas::object_media_url(*frame.objects[1].object)));

    const QImage rendered = render_scene_frame_image(frame, 200, 100, Qt::black, with_cache(cache));
    REQUIRE(nearly_rgb(rendered.pixel(30, 30), 255, 0, 0));
    REQUIRE(nearly_rgb(rendered.pixel(100, 30), 0, 255, 0));
}

TEST_CASE("A text box's fill and line paint behind its words, where the words are",
          "[render][freeform][fill][line]")
{
    seventhslide::test::ensure_qt_application();

    // A text object with a frame: the scene carries the object for the frame
    // and the laid-out box for the words.
    seventhslide::canvas::Object text;
    text.type = seventhslide::canvas::ObjectType::Text;
    text.x = 20;
    text.y = 20;
    text.w = 160;
    text.h = 60;
    text.fill.color = "#0000ff";
    text.line.color = "#ff0000";
    text.line.width = 4;
    REQUIRE(seventhslide::canvas::paints_frame(text));

    seventhslide::layout::OverlayBox words;
    static_cast<seventhslide::canvas::BoxRect&>(words) = text;
    static_cast<seventhslide::canvas::BoxType&>(words) = text;
    words.font_size = 12;
    words.font_color = "#ffffff";
    words.lines.push_back({seventhslide::model::TextSpan{"Hi", {}}});

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneObject so;
    so.id = "t1";
    so.object = text;
    so.text = words;
    frame.objects = {so};

    const auto render = [&frame] {
        seventhslide::render::ElementRasterCache rasters;
        seventhslide::render::SceneResources res;
        res.rasters = &rasters;
        return render_scene_frame_image(frame, 200, 100, Qt::black, res);
    };

    const QImage painted = render();
    // Inside the box, away from the words: the fill. On its top edge: the line.
    REQUIRE(nearly_rgb(painted.pixel(30, 70), 0, 0, 255));
    REQUIRE(nearly_rgb(painted.pixel(100, 20), 255, 0, 0));
    // Outside the box: nothing.
    REQUIRE(nearly_rgb(painted.pixel(10, 10), 0, 0, 0));

    // The frame turns with the object, about its middle: a quarter turn
    // stands it on end.
    frame.objects[0].object->rotation = 90;
    const QImage turned = render();
    REQUIRE(nearly_rgb(turned.pixel(30, 70), 0, 0, 0));
    REQUIRE(nearly_rgb(turned.pixel(100, 85), 0, 0, 255));

    // Without a frame the words paint over nothing.
    frame.objects[0].object.reset();
    const QImage bare = render();
    REQUIRE(nearly_rgb(bare.pixel(30, 70), 0, 0, 0));
}

TEST_CASE("A picture is cropped, recoloured and made see-through as it is placed",
          "[render][freeform][picture]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    std::filesystem::create_directories(root / "images");
    // Red on the left half, green on the right.
    QImage src(40, 20, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(255, 0, 0));
    {
        QPainter p(&src);
        p.fillRect(20, 0, 20, 20, QColor(0, 255, 0));
    }
    REQUIRE(src.save(QString::fromStdString((root / "images" / "halves.png").string())));
    BackgroundImageCache cache;
    cache.set_static_root(root);

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneObject picture;
    picture.id = "p";
    picture.object = image_box(20, 20, 80, 40, "halves.png", "/static/images/halves.png", "fill");
    frame.objects = {picture};
    REQUIRE(cache.ensure("/static/images/halves.png"));
    const auto render = [&] {
        return render_scene_frame_image(frame, 200, 100, Qt::black, with_cache(cache));
    };

    const QImage whole = render();
    REQUIRE(nearly_rgb(whole.pixel(40, 40), 255, 0, 0));
    REQUIRE(nearly_rgb(whole.pixel(80, 40), 0, 255, 0));

    // The left half cropped off: the whole box is the green half.
    frame.objects[0].object->picture.crop.left = 0.5;
    const QImage cropped = render();
    REQUIRE(nearly_rgb(cropped.pixel(40, 40), 0, 255, 0));
    REQUIRE(nearly_rgb(cropped.pixel(80, 40), 0, 255, 0));
    frame.objects[0].object->picture.crop.left = 0;

    // Grayscale: the red is a grey; a tint: that grey in the tint's colour.
    frame.objects[0].object->picture.recolor = seventhslide::canvas::Recolor::Grayscale;
    const QRgb grey = render().pixel(40, 40);
    CHECK(std::abs(qRed(grey) - qGreen(grey)) <= 2);
    CHECK(std::abs(qRed(grey) - qBlue(grey)) <= 2);
    CHECK(qRed(grey) > 40);
    frame.objects[0].object->picture.recolor = seventhslide::canvas::Recolor::Tint;
    frame.objects[0].object->picture.tint = "#0000ff";
    const QRgb tinted = render().pixel(40, 40);
    CHECK(qBlue(tinted) > 40);
    CHECK(qRed(tinted) < 10);
    frame.objects[0].object->picture.recolor = seventhslide::canvas::Recolor::None;

    // Half transparent over black: half as bright.
    frame.objects[0].object->picture.transparency = 0.5;
    const QRgb dim = render().pixel(40, 40);
    CHECK(qRed(dim) > 100);
    CHECK(qRed(dim) < 160);
    CHECK(qGreen(dim) < 10);
}

TEST_CASE("A picture fill is fitted and clipped to the shape", "[render][freeform][picture][fill]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    std::filesystem::create_directories(root / "images");
    QImage src(20, 20, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(0, 0, 255));
    REQUIRE(src.save(QString::fromStdString((root / "images" / "blue.png").string())));
    BackgroundImageCache cache;
    cache.set_static_root(root);
    REQUIRE(cache.ensure("/static/images/blue.png"));

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneObject ellipse;
    ellipse.id = "e";
    ellipse.object = shape_box(20, 10, 160, 80, "#ff0000");
    ellipse.object->shape_kind = seventhslide::geom::ShapeKind::Ellipse;
    ellipse.object->line.color.clear();
    ellipse.object->fill.kind = seventhslide::canvas::FillKind::Picture;
    ellipse.object->fill.picture.file = "blue.png";
    ellipse.object->fill.picture.fit = "cover";
    frame.objects = {ellipse};
    REQUIRE(seventhslide::canvas::fill_picture_url(ellipse.object->fill)
            == std::string("/static/images/blue.png"));

    const QImage painted = render_scene_frame_image(frame, 200, 100, Qt::black, with_cache(cache));
    // The centre is the picture, not the colour; the box's corner, outside
    // the ellipse, is untouched.
    REQUIRE(nearly_rgb(painted.pixel(100, 50), 0, 0, 255));
    REQUIRE(nearly_rgb(painted.pixel(24, 14), 0, 0, 0));

    // A text box's frame takes a picture fill the same way.
    SceneObject text;
    text.id = "t";
    text.object = *ellipse.object;
    text.object->type = seventhslide::canvas::ObjectType::Text;
    frame.objects = {text};
    REQUIRE(seventhslide::canvas::paints_frame(*text.object));
    const QImage framed = render_scene_frame_image(frame, 200, 100, Qt::black, with_cache(cache));
    REQUIRE(nearly_rgb(framed.pixel(24, 14), 0, 0, 255));
}

TEST_CASE("Freeform text and shapes stack by z, not by kind", "[render][freeform][zorder]")
{
    seventhslide::test::ensure_qt_application();

    // A red shape and a text box on the same patch of canvas. Whichever has the
    // higher z must win - the two used to paint in separate passes, which put
    // every shape in front of every text box no matter what the editor showed.
    const auto build = [](int shape_z, int text_z) {
        SceneFrame frame;
        frame.mode = SceneMode::Freeform;
        frame.canvas_width = 200;
        frame.canvas_height = 100;
        frame.background.type = seventhslide::layout::BackgroundType::Color;
        frame.background.color = "#000000";

        SceneObject shape;
        shape.id = "shape";
        shape.z = shape_z;
        shape.object = shape_box(0, 0, 200, 100, "#ff0000");

        seventhslide::layout::OverlayBox box;
        box.x = 0;
        box.y = 0;
        box.w = 200;
        box.h = 100;
        box.font_family = "Liberation Sans";
        box.font_size = 60;
        box.font_color = "#0000ff";
        box.text_align = "center";
        box.vertical_align = "middle";
        box.id = "text";
        seventhslide::model::TextSpan line;
        line.text = "HELLO";
        box.lines.push_back({std::move(line)});

        SceneObject text;
        text.id = "text";
        text.z = text_z;
        text.text = std::move(box);

        frame.objects = {std::move(shape), std::move(text)};
        return frame;
    };

    const auto blue_pixels = [](const QImage& img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = img.pixel(x, y);
                if (qBlue(px) > 120 && qRed(px) < 120) {
                    ++n;
                }
            }
        }
        return n;
    };

    const QImage text_on_top = render_scene_frame_image(build(0, 1), 200, 100);
    const QImage shape_on_top = render_scene_frame_image(build(1, 0), 200, 100);

    REQUIRE(blue_pixels(text_on_top) > 0);
    REQUIRE(blue_pixels(shape_on_top) == 0);
    REQUIRE(nearly_rgb(shape_on_top.pixel(100, 50), 255, 0, 0));
}

TEST_CASE("Freeform stacking survives a content transition", "[render][freeform][zorder]")
{
    using seventhslide::render::paint_objects_transitioning;
    seventhslide::test::ensure_qt_application();

    // Ids used to drive the transition paint order, so "shape" sorted before
    // "text" regardless of z. Order must come from z on both paths.
    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;

    SceneObject shape;
    shape.id = "shape";
    shape.z = 0;
    shape.object = shape_box(0, 0, 200, 100, "#ff0000");

    seventhslide::layout::OverlayBox box;
    box.x = 0;
    box.y = 0;
    box.w = 200;
    box.h = 100;
    box.font_family = "Liberation Sans";
    box.font_size = 60;
    box.font_color = "#0000ff";
    box.text_align = "center";
    box.vertical_align = "middle";
    box.id = "text";
    seventhslide::model::TextSpan line;
    line.text = "HELLO";
    box.lines.push_back({std::move(line)});

    SceneObject text;
    text.id = "text";
    text.z = 1;
    text.text = std::move(box);
    frame.objects = {std::move(shape), std::move(text)};

    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_objects_transitioning(painter, frame, frame, 1.0, {}, {});
    painter.end();

    int blue = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qBlue(px) > 120 && qRed(px) < 120) {
                ++blue;
            }
        }
    }
    REQUIRE(blue > 0);
}

TEST_CASE("Image and video areas cover the canvas on an axis whose size is not positive", "[render]")
{
    using seventhslide::render::video_area_rect;
    SceneFrame frame;
    frame.canvas_width = 1280;
    frame.canvas_height = 720;
    // No media at all: the whole canvas.
    CHECK(image_area_rect(frame) == QRectF(0, 0, 1280, 720));
    CHECK(video_area_rect(frame) == QRectF(0, 0, 1280, 720));

    SceneImageMedia image;
    image.area = {.enabled = true, .x = 10, .y = 20, .width = -5, .height = 30};
    frame.image = image;
    CHECK(image_area_rect(frame) == QRectF(10, 20, 1280, 30));

    seventhslide::scene::SceneVideoMedia video;
    video.area = {.enabled = true, .x = 3, .y = 4, .width = 640, .height = -1};
    frame.video = video;
    CHECK(video_area_rect(frame) == QRectF(3, 4, 640, 720));
}

TEST_CASE("A rotated or flipped object paints about its own centre, live or cached",
          "[render][freeform]")
{
    seventhslide::test::ensure_qt_application();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const auto root = std::filesystem::path(tmp.path().toStdString());
    std::filesystem::create_directories(root / "images");
    // Blue on the left, green on the right.
    QImage halves(20, 20, QImage::Format_ARGB32_Premultiplied);
    halves.fill(QColor(0, 0, 255));
    {
        QPainter p(&halves);
        p.fillRect(QRect(10, 0, 10, 20), QColor(0, 255, 0));
    }
    REQUIRE(halves.save(QString::fromStdString((root / "images" / "halves.png").string())));
    BackgroundImageCache cache;
    cache.set_static_root(root);

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 200;
    frame.canvas_height = 100;
    // An 80x20 bar turned a quarter about its centre (100, 50): 20 wide, 80 tall.
    SceneObject bar;
    bar.object = shape_box(60, 40, 80, 20, "#ff0000");
    bar.object->rotation = 90;
    // The halves image, mirrored: green now on the left.
    SceneObject photo;
    photo.z = 1;
    photo.object = image_box(150, 30, 40, 40, "halves.png", "/static/images/halves.png", "fill");
    photo.object->flip_h = true;
    frame.objects = {bar, photo};
    REQUIRE(cache.ensure(*seventhslide::canvas::object_media_url(*photo.object)));

    seventhslide::render::ElementRasterCache rasters;
    for (auto* r : {static_cast<seventhslide::render::ElementRasterCache*>(nullptr), &rasters, &rasters}) {
        INFO((r ? "cached" : "live"));
        QImage out(200, 100, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::black);
        QPainter p(&out);
        seventhslide::render::paint_scene_objects(p, frame, {.cache = &cache, .rasters = r});
        p.end();
        CHECK(nearly_rgb(out.pixel(100, 15), 255, 0, 0));   // the turned bar's top
        CHECK(nearly_rgb(out.pixel(100, 85), 255, 0, 0));   // and bottom
        CHECK(nearly_rgb(out.pixel(65, 50), 0, 0, 0));      // where the bar lay before turning
        CHECK(nearly_rgb(out.pixel(155, 50), 0, 255, 0));   // mirrored: green left
        CHECK(nearly_rgb(out.pixel(185, 50), 0, 0, 255));   // blue right
    }
}
