#include <catch2/catch_test_macros.hpp>

#include "font_fixtures.hpp"
#include "pixel_compare.hpp"
#include "test_qt_app.hpp"

#include <seventhslide/fonts/font_registry.hpp>
#include <seventhslide/layout/chords.hpp>
#include <seventhslide/layout/qt_font_faces.hpp>
#include <seventhslide/layout/overlay_layout.hpp>
#include <seventhslide/layout/qt_text_measurer.hpp>
#include <seventhslide/render/element_raster_cache.hpp>
#include <seventhslide/render/image_painter.hpp>
#include <seventhslide/render/scene_compositor.hpp>
#include <seventhslide/render/text_painter.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/scene/text_metrics.hpp>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QPainter>
#include <QTime>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdint>

using seventhslide::layout::parse_chord_line;
using seventhslide::model::RichLine;
using seventhslide::model::TextSpan;
using seventhslide::model::TextStyle;
using seventhslide::render::paint_scene_objects;
using seventhslide::render::paint_text_content;
using seventhslide::render::render_scene_frame_image;
#include "scene_object_helpers.hpp"
using seventhslide::scene::SceneFrame;
using seventhslide::scene::SceneMode;
using seventhslide::scene::SceneObject;
using seventhslide::scene::SceneTextBoxStyle;
using seventhslide::scene::SceneTextContent;
using seventhslide::scene::SceneTextLine;

namespace {

/// Each of ``overlay``'s boxes, in order, as a title slide's paint.
void paint_overlay_scene(QPainter& painter,
                         const seventhslide::layout::OverlayScene& overlay,
                         const seventhslide::render::PaintContext& ctx = {})
{
    for (const auto& box : overlay.boxes) {
        seventhslide::render::paint_overlay_box(painter, box, ctx);
    }
}

[[nodiscard]] bool is_near_black(QRgb px) noexcept
{
    return qRed(px) < 8 && qGreen(px) < 8 && qBlue(px) < 8;
}

/// Pixels with a channel of 8 or more.
[[nodiscard]] int count_ink(const QImage& img)
{
    return seventhslide::test::count_ink(img, 7);
}

[[nodiscard]] bool any_ink_in(const QImage& img, int x0, int y0, int x1, int y1)
{
    const int left = std::max(0, x0);
    const int top = std::max(0, y0);
    const int right = std::min(img.width(), x1);
    const int bottom = std::min(img.height(), y1);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            if (!is_near_black(img.pixel(x, y))) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] bool any_ink_outside(const QImage& img, int x0, int y0, int x1, int y1)
{
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                continue;
            }
            if (!is_near_black(img.pixel(x, y))) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] int rightmost_ink_x(const QImage& img)
{
    int right = -1;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = img.width() - 1; x >= 0; --x) {
            if (x <= right) {
                break;
            }
            if (!is_near_black(img.pixel(x, y))) {
                right = x;
                break;
            }
        }
    }
    return right;
}

[[nodiscard]] double ink_centroid_x(const QImage& img)
{
    double sx = 0;
    double n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (!is_near_black(img.pixel(x, y))) {
                sx += x;
                n += 1;
            }
        }
    }
    REQUIRE(n > 0);
    return sx / n;
}

[[nodiscard]] double ink_centroid_y(const QImage& img)
{
    double sy = 0;
    double n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (!is_near_black(img.pixel(x, y))) {
                sy += y;
                n += 1;
            }
        }
    }
    REQUIRE(n > 0);
    return sy / n;
}

[[nodiscard]] SceneTextContent make_line_box(SceneTextBoxStyle box, const std::string& text)
{
    SceneTextContent content;
    content.box = std::move(box);
    SceneTextLine line;
    line.runs = RichLine{TextSpan{text, {}}};
    content.lines.push_back(std::move(line));
    return content;
}

}  // namespace

TEST_CASE("paint_text_content draws ink with clip/align/valign", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SECTION("centered line produces ink")
    {
        SceneTextBoxStyle box;
        box.x = 0;
        box.y = 0;
        box.width = 400;
        box.height = 200;
        box.font_family = "Liberation Sans";
        box.font_size = 48;
        box.color = "#ffffff";
        box.align = "center";
        box.valign = "middle";

        QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_text_content(painter, make_line_box(box, "Hello"));
        painter.end();

        REQUIRE(count_ink(img) > 50);
    }

    SECTION("overflow is clipped to the box")
    {
        SceneTextBoxStyle box;
        box.x = 40;
        box.y = 40;
        box.width = 80;
        box.height = 36;
        box.font_family = "Liberation Sans";
        box.font_size = 64;
        box.color = "#ffffff";
        box.align = "left";
        box.valign = "top";

        QImage img(240, 180, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_text_content(painter, make_line_box(box, "CLIPME"));
        painter.end();

        REQUIRE(any_ink_in(img, 40, 40, 120, 76));
        REQUIRE_FALSE(any_ink_outside(img, 40, 40, 120, 76));
    }

    SECTION("left vs right align shifts ink")
    {
        auto paint_align = [](const std::string& align) {
            SceneTextBoxStyle box;
            box.x = 0;
            box.y = 0;
            box.width = 400;
            box.height = 120;
            box.font_family = "Liberation Sans";
            box.font_size = 40;
            box.color = "#ffffff";
            box.align = align;
            box.valign = "middle";

            QImage img(400, 120, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::black);
            QPainter painter(&img);
            paint_text_content(painter, make_line_box(box, "Align"));
            painter.end();
            return img;
        };

        const QImage left = paint_align("left");
        const QImage right = paint_align("right");
        REQUIRE(ink_centroid_x(left) < ink_centroid_x(right) - 40.0);
    }

    SECTION("top vs bottom valign shifts ink")
    {
        auto paint_valign = [](const std::string& valign) {
            SceneTextBoxStyle box;
            box.x = 0;
            box.y = 0;
            box.width = 300;
            box.height = 240;
            box.font_family = "Liberation Sans";
            box.font_size = 36;
            box.color = "#ffffff";
            box.align = "center";
            box.valign = valign;

            QImage img(300, 240, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::black);
            QPainter painter(&img);
            paint_text_content(painter, make_line_box(box, "Vert"));
            painter.end();
            return img;
        };

        const QImage top = paint_valign("top");
        const QImage bottom = paint_valign("bottom");
        REQUIRE(ink_centroid_y(top) < ink_centroid_y(bottom) - 40.0);
    }

    SECTION("chord line paints without crash")
    {
        SceneTextContent content;
        content.box.x = 0;
        content.box.y = 0;
        content.box.width = 500;
        content.box.height = 160;
        content.box.font_family = "Liberation Sans";
        content.box.font_size = 40;
        content.box.color = "#ffffff";
        content.box.align = "left";
        content.box.valign = "top";

        SceneTextLine line;
        line.has_chords = true;
        line.chords = parse_chord_line("[C]Hello [G]world");
        content.lines.push_back(std::move(line));

        QImage img(500, 160, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_text_content(painter, content);
        painter.end();
        REQUIRE(count_ink(img) > 50);
    }
}

TEST_CASE("A body box's fill and line paint behind its words", "[render][fill][line]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = 400;
    frame.canvas_height = 200;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";
    SceneTextContent text;
    text.box = {40, 40, 320, 120, "Liberation Sans", 20};
    text.box.fill.color = "#0000ff";
    text.box.line.color = "#ff0000";
    text.box.line.width = 4;
    SceneTextLine line;
    line.runs = {TextSpan{"Hi", {}}};
    text.lines = {line};
    frame.text = text;

    const auto render = [&frame] {
        seventhslide::render::ElementRasterCache rasters;
        seventhslide::render::SceneResources res;
        res.rasters = &rasters;
        return render_scene_frame_image(frame, 400, 200, Qt::black, res);
    };
    const QImage painted = render();
    // Inside the box, off the words: the fill. On the edge: the line, its
    // outer half included. Outside: nothing.
    CHECK(qBlue(painted.pixel(60, 140)) > 200);
    CHECK(qRed(painted.pixel(200, 40)) > 200);
    CHECK(qRed(painted.pixel(200, 38)) > 200);
    CHECK(qRed(painted.pixel(200, 30)) < 30);
    CHECK(qBlue(painted.pixel(200, 30)) < 30);

    // A fill of kind none paints only the line.
    frame.text->box.fill.kind = seventhslide::canvas::FillKind::None;
    const QImage lined = render();
    CHECK(qBlue(lined.pixel(60, 140)) < 30);
    CHECK(qRed(lined.pixel(200, 40)) > 200);

    // With margins the frame is the element's rectangle: 20 px out from the
    // words' box on the left.
    frame.text->box.fill.kind = seventhslide::canvas::FillKind::Solid;
    frame.text->box.line.color.clear();
    frame.text->box.margins.left = 20;
    const QImage framed = render();
    CHECK(qBlue(framed.pixel(25, 100)) > 200);
    CHECK(qBlue(framed.pixel(15, 100)) < 30);
}

TEST_CASE("paint_text_content raises bible verse superscript spans", "[render][bible][verse]")
{
    seventhslide::test::ensure_qt_application();

    auto paint = [](int rise_pct) {
        SceneTextContent content;
        content.box.x = 0;
        content.box.y = 0;
        content.box.width = 400;
        content.box.height = 120;
        content.box.font_family = "Liberation Sans";
        content.box.font_size = 48;
        content.box.color = "#ffffff";
        content.box.align = "left";
        content.box.valign = "bottom";

        SceneTextLine line;
        TextSpan verse;
        verse.text = "3:16";
        verse.style.size_pct = 50;
        verse.style.baseline_rise_pct = rise_pct;
        verse.style.margin_right_pct = 40;
        verse.style.color = "#ffffff";
        line.runs.push_back(std::move(verse));
        TextSpan body;
        body.text = "AAAA";
        line.runs.push_back(std::move(body));
        content.lines.push_back(std::move(line));

        QImage img(400, 120, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_text_content(painter, content);
        painter.end();
        return img;
    };

    const QImage baseline = paint(0);
    const QImage raised = paint(40);
    // Raised verse ink should sit higher (smaller y) than baseline-aligned.
    REQUIRE(ink_centroid_y(raised) < ink_centroid_y(baseline) - 2.0);
}

TEST_CASE("Title boxes raise superscript spans", "[render][freeform][super]")
{
    seventhslide::test::ensure_qt_application();

    auto paint = [](int rise_pct) {
        seventhslide::layout::OverlayScene overlay;
        seventhslide::layout::OverlayBox box;
        box.x = 0;
        box.y = 0;
        box.w = 400;
        box.h = 120;
        box.font_family = "Liberation Sans";
        box.font_size = 48;
        box.font_color = "#ffffff";
        box.text_align = "left";
        box.vertical_align = "bottom";

        seventhslide::model::RichLine line;
        seventhslide::model::TextSpan verse;
        verse.text = "3:16";
        verse.style.size_pct = 50;
        verse.style.baseline_rise_pct = rise_pct;
        verse.style.color = "#ffffff";
        line.push_back(std::move(verse));
        seventhslide::model::TextSpan body;
        body.text = "AAAA";
        line.push_back(std::move(body));
        box.lines.push_back(std::move(line));
        overlay.boxes.push_back(std::move(box));

        QImage img(400, 120, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_overlay_scene(painter, overlay);
        painter.end();
        return img;
    };

    const QImage baseline = paint(0);
    const QImage raised = paint(40);
    REQUIRE(ink_centroid_y(raised) < ink_centroid_y(baseline) - 2.0);
}

TEST_CASE("Title boxes paint in canvas-px boxes", "[render]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::layout::OverlayScene overlay;
    seventhslide::layout::OverlayBox box;
    box.x = 20;
    box.y = 20;
    box.w = 200;
    box.h = 80;
    box.font_family = "Liberation Sans";
    box.font_size = 32;
    box.font_color = "#ffffff";
    box.text_align = "center";
    box.vertical_align = "middle";
    box.lines.push_back({seventhslide::model::TextSpan{"Title", {}}});
    overlay.boxes.push_back(std::move(box));

    QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_overlay_scene(painter, overlay);
    painter.end();

    REQUIRE(any_ink_in(img, 20, 20, 220, 100));
    REQUIRE_FALSE(any_ink_outside(img, 20, 20, 220, 100));
}

TEST_CASE("Title boxes wrap long lines to their width", "[render][title]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::layout::OverlayScene overlay;
    seventhslide::layout::OverlayBox box;
    box.x = 10;
    box.y = 10;
    box.w = 120;
    box.h = 160;
    box.font_family = "Liberation Sans";
    box.font_size = 28;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.vertical_align = "top";
    box.line_height = 1.15;
    // Wider than one line at 28px in a 120px box - must wrap (word + break-word).
    box.lines.push_back({seventhslide::model::TextSpan{
        "Amazing Grace How Sweet the Sound That Saved", {}}});
    overlay.boxes.push_back(std::move(box));

    QImage img(200, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_overlay_scene(painter, overlay);
    painter.end();

    REQUIRE(any_ink_in(img, 10, 10, 130, 170));
    REQUIRE_FALSE(any_ink_outside(img, 10, 10, 130, 170));

    // Expect ink on more than one vertical band (wrapped rows).
    int ink_rows = 0;
    for (int y = 10; y < 170; ++y) {
        bool row_ink = false;
        for (int x = 10; x < 130; ++x) {
            if (!is_near_black(img.pixel(x, y))) {
                row_ink = true;
                break;
            }
        }
        if (row_ink) {
            ++ink_rows;
        }
    }
    REQUIRE(ink_rows > 40);
}

TEST_CASE("a frame's body paints, and a blank frame's does not", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = 400;
    frame.canvas_height = 300;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = 400;
    text.box.height = 300;
    text.box.font_family = "Liberation Sans";
    text.box.font_size = 48;
    text.box.color = "#ffffff";
    text.box.align = "center";
    text.box.valign = "middle";
    SceneTextLine line;
    line.runs = RichLine{TextSpan{"Live", {}}};
    text.lines.push_back(std::move(line));
    frame.text = text;

    SECTION("normal frame paints ink")
    {
        const QImage img = render_scene_frame_image(frame, 400, 300);
        REQUIRE(count_ink(img) > 50);
    }

    SECTION("blank suppresses text")
    {
        frame.is_blank = true;
        const QImage img = render_scene_frame_image(frame, 400, 300);
        REQUIRE(count_ink(img) == 0);
    }
}

TEST_CASE("a body span rgb() paints that color, not the box color", "[render][color]")
{
    seventhslide::test::ensure_qt_application();

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = 200;
    text.box.height = 80;
    text.box.font_family = "Liberation Sans";
    text.box.font_size = 64;
    text.box.color = "#0000ff";
    text.box.align = "left";
    text.box.valign = "top";
    SceneTextLine line;
    TextStyle style;
    style.color = "rgb(255, 0, 0)";
    line.runs = RichLine{TextSpan{"M", style}};
    text.lines.push_back(std::move(line));

    QImage img(200, 80, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_text_content(painter, text);
    painter.end();

    bool red = false;
    bool blue = false;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qRed(px) > 200 && qGreen(px) < 40 && qBlue(px) < 40) {
                red = true;
            }
            if (qBlue(px) > 200 && qRed(px) < 40 && qGreen(px) < 40) {
                blue = true;
            }
        }
    }
    REQUIRE(red);
    REQUIRE_FALSE(blue);
}

// A themed reference box is a text object on the frame, so it paints with the
// object stack - the same code a Freeform text box goes through.
TEST_CASE("a themed text element paints in its box", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Bible;
    frame.canvas_width = 400;
    frame.canvas_height = 300;
    frame.background.type = seventhslide::layout::BackgroundType::Color;
    frame.background.color = "#000000";

    seventhslide::layout::OverlayBox box;
    box.x = 10;
    box.y = 220;
    box.w = 380;
    box.h = 70;
    box.font_family = "Liberation Sans";
    box.font_size = 28;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.vertical_align = "middle";
    box.lines.push_back({seventhslide::model::TextSpan{"John 3:16", {}}});
    box.lines.push_back({seventhslide::model::TextSpan{"KJV", {}}});
    SceneObject so;
    so.id = "te_bible_ref";
    so.text = box;
    frame.objects.push_back(std::move(so));

    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_scene_objects(painter, frame);
    painter.end();

    REQUIRE(any_ink_in(img, 10, 220, 390, 290));
    REQUIRE_FALSE(any_ink_in(img, 0, 0, 400, 200));
}

TEST_CASE("a themed text element and clock paint together", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = 400;
    frame.canvas_height = 300;

    seventhslide::layout::OverlayBox box;
    box.x = 10;
    box.y = 220;
    box.w = 380;
    box.h = 70;
    box.font_family = "Liberation Sans";
    box.font_size = 20;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.vertical_align = "middle";
    box.lines.push_back({seventhslide::model::TextSpan{"Writer", {}}});
    box.lines.push_back({seventhslide::model::TextSpan{"© 2020", {}}});
    SceneObject text_obj;
    text_obj.id = "te_copyright";
    text_obj.text = box;
    frame.objects.push_back(std::move(text_obj));

    seventhslide::canvas::Object clk = seventhslide::test::scene_clock();
    clk.x = 10;
    clk.y = 10;
    clk.font_size = 28;
    clk.font_family = "Liberation Sans";
    clk.font_color = "#ffffff";
    clk.clock_24h = true;
    SceneObject clock_obj;
    clock_obj.id = "te_clock";
    clock_obj.object = clk;
    frame.objects.push_back(std::move(clock_obj));

    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_scene_objects(painter, frame,
                        {.now = QDateTime(QDate(2026, 7, 30), QTime(14, 5, 9))});
    painter.end();

    REQUIRE(any_ink_in(img, 10, 220, 390, 290));
    REQUIRE(any_ink_in(img, 10, 10, 120, 50));
}

TEST_CASE("paint_scene_objects draws clock objects from QDateTime", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 400;
    frame.canvas_height = 300;
    seventhslide::canvas::Object clk = seventhslide::test::scene_clock();
    clk.x = 10;
    clk.y = 10;
    clk.font_size = 28;
    clk.font_family = "Liberation Sans";
    clk.font_color = "#ffffff";
    clk.clock_24h = true;
    clk.clock_seconds = false;
    SceneObject so;
    so.object = clk;
    frame.objects.push_back(std::move(so));

    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    // Every clock is an object: it paints with the z-ordered stack, at its own
    // slot, whether it came from a Freeform deck or a text theme.
    paint_scene_objects(painter, frame,
                        {.now = QDateTime(QDate(2026, 7, 30), QTime(14, 5, 9))});
    painter.end();

    REQUIRE(any_ink_in(img, 10, 10, 120, 50));
    REQUIRE_FALSE(any_ink_in(img, 200, 200, 390, 290));
}

TEST_CASE("clock objects honour bold and italic", "[render]")
{
    seventhslide::test::ensure_qt_application();

    const QDateTime when(QDate(2026, 7, 30), QTime(14, 5, 9));
    auto render = [&](bool bold, bool italic) {
        SceneFrame frame;
        frame.canvas_width = 400;
        frame.canvas_height = 300;
        frame.mode = SceneMode::Freeform;
        seventhslide::canvas::Object clk = seventhslide::test::scene_clock();
        clk.x = 10;
        clk.y = 10;
        clk.font_size = 40;
        clk.font_family = "Liberation Sans";
        clk.font_color = "#ffffff";
        clk.bold = bold;
        clk.italic = italic;
        clk.clock_24h = true;
        SceneObject so;
        so.object = clk;
        frame.objects.push_back(std::move(so));

        QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        paint_scene_objects(painter, frame, {.now = when});
        painter.end();
        return img;
    };

    const QImage plain = render(false, false);
    const QImage bold = render(true, false);
    const QImage italic = render(false, true);
    REQUIRE(count_ink(plain) > 0);
    // Bold is heavier, italic is merely different - both must move the raster.
    REQUIRE(count_ink(bold) > count_ink(plain));
    REQUIRE(italic != plain);
}

TEST_CASE("an indicator object draws active verse codes", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Song;
    frame.canvas_width = 400;
    frame.canvas_height = 300;

    seventhslide::layout::OverlayBox box;
    box.x = 10;
    box.y = 250;
    box.w = 200;
    box.h = 40;
    box.font_family = "Liberation Sans";
    box.font_size = 24;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.vertical_align = "top";
    seventhslide::model::RichLine line;
    seventhslide::model::TextSpan active;
    active.text = "V1 ";
    active.style.bold = true;
    seventhslide::model::TextSpan other;
    other.text = "C1";
    other.style.color = "#808080";
    line.push_back(std::move(active));
    line.push_back(std::move(other));
    box.lines.push_back(std::move(line));
    SceneObject so;
    so.id = "te_indicator";
    so.text = std::move(box);
    frame.objects.push_back(std::move(so));

    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_scene_objects(painter, frame);
    painter.end();

    REQUIRE(any_ink_in(img, 10, 240, 200, 290));
}

TEST_CASE("inline bold is heavier than regular without ghosting", "[render][bold]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    auto paint_glyph = [&](const std::string& family, bool bold) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = 400;
        text.box.height = 200;
        text.box.font_family = family;
        text.box.font_size = 110;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        SceneTextLine line;
        TextStyle st;
        st.bold = bold;
        line.runs = RichLine{TextSpan{"I", st}};
        text.lines.push_back(std::move(line));

        QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        paint_text_content(p, text, {.faces = &faces});
        p.end();
        return img;
    };

    const std::string family = "Liberation Sans";

    const QImage bold_img = paint_glyph(family, true);
    const QImage plain_img = paint_glyph(family, false);
    const int bold_ink = count_ink(bold_img);
    const int plain_ink = count_ink(plain_img);
    INFO("family=" << family << " bold_ink=" << bold_ink << " plain_ink=" << plain_ink);
    REQUIRE(plain_ink > 0);
    REQUIRE(bold_ink > plain_ink);

    // Ghosting was a second draw shifted by ~4% em. Extra ink from a real
    // weight change is a thicker stem, not a copy of the regular glyph.
    const int offset = std::max(1, static_cast<int>(std::lround(110.0 * 0.04)));
    int extra = 0;
    int extra_explained_by_shift = 0;
    for (int y = 0; y < plain_img.height(); ++y) {
        for (int x = 0; x < plain_img.width(); ++x) {
            const bool bold_px = !is_near_black(bold_img.pixel(x, y));
            const bool plain_px = !is_near_black(plain_img.pixel(x, y));
            if (!bold_px || plain_px) {
                continue;
            }
            ++extra;
            const int sx = x - offset;
            if (sx >= 0 && !is_near_black(plain_img.pixel(sx, y))) {
                ++extra_explained_by_shift;
            }
        }
    }
    INFO("extra=" << extra << " shift_overlap=" << extra_explained_by_shift);
    REQUIRE(extra > 0);
    REQUIRE(extra_explained_by_shift < extra * 0.75);
}

TEST_CASE("mixed-weight runs paint heavier than plain at Main size", "[render][bold]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    SceneTextContent text;
    text.box.x = 0;
    text.box.y = 0;
    text.box.width = 1800;
    text.box.height = 200;
    text.box.font_family = "EB Garamond";
    text.box.font_size = 110;
    text.box.color = "#ffffff";
    text.box.align = "left";
    text.box.valign = "top";

    SceneTextLine line;
    TextStyle bold_style;
    bold_style.bold = true;
    line.runs = RichLine{
        TextSpan{"Early ", {}},
        TextSpan{"morning", bold_style},
        TextSpan{" song", {}},
    };
    text.lines.push_back(std::move(line));

    QImage bold_img(1800, 200, QImage::Format_ARGB32_Premultiplied);
    bold_img.fill(Qt::black);
    {
        QPainter p(&bold_img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        paint_text_content(p, text, {.faces = &faces});
    }

    SceneTextContent plain = text;
    plain.lines[0].runs = RichLine{TextSpan{"Early morning song", {}}};
    QImage plain_img(1800, 200, QImage::Format_ARGB32_Premultiplied);
    plain_img.fill(Qt::black);
    {
        QPainter p(&plain_img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        paint_text_content(p, plain, {.faces = &faces});
    }

    const int bold_ink = count_ink(bold_img);
    const int plain_ink = count_ink(plain_img);
    INFO("bold_ink=" << bold_ink << " plain_ink=" << plain_ink);
    REQUIRE(bold_ink > plain_ink);
}

TEST_CASE("preview-scale glyphs keep design-size advance", "[render][preview]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    SceneTextBoxStyle box;
    box.x = 0;
    box.y = 0;
    box.width = 1800;
    box.height = 120;
    box.font_family = "Liberation Sans";
    box.font_size = 48;
    box.color = "#ffffff";
    box.align = "left";
    box.valign = "top";

    const auto content = make_line_box(
        box,
        "1:27 He it is, who coming after me is preferred before me, whose sandal strap I am not worthy to loose.");

    auto paint_at = [&](double scale, int img_w, int img_h) {
        QImage img(img_w, img_h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.scale(scale, scale);
        paint_text_content(p, content, {.faces = &faces});
        p.end();
        return img;
    };

    const QImage full = paint_at(1.0, 1800, 120);
    const int full_right = rightmost_ink_x(full);
    REQUIRE(full_right > 400);

    const double preview_scale = 320.0 / 1920.0;
    const QImage preview = paint_at(preview_scale, 320, 40);
    const int preview_right = rightmost_ink_x(preview);
    REQUIRE(preview_right > 0);

    const double preview_design_right = static_cast<double>(preview_right) / preview_scale;
    INFO("full_right=" << full_right << " preview_design_right=" << preview_design_right);
    // One preview pixel is ~6 design pixels. Integer snap without advance
    // matching overruns ~4% (tens of design pixels) and clips verses that
    // fill the theme box.
    REQUIRE(std::abs(preview_design_right - static_cast<double>(full_right))
            < std::max(12.0, 0.015 * static_cast<double>(full_right)));
}

TEST_CASE("faux bold keeps weight under preview-scale transforms", "[render][bold][preview]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_fixture_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    // A family with no Bold anywhere: bold is the faux thicken.
    const std::string family(seventhslide::test::k_fixture_plain);
    const auto probe = faces.resolve(
        seventhslide::layout::FontStyle{.family = family, .size_px = 110.0, .bold = true});
    REQUIRE(probe.needs_synthetic_embolden());

    auto make_content = [&](bool bold) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = 800;
        text.box.height = 200;
        text.box.font_family = family;
        text.box.font_size = 110;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        SceneTextLine line;
        TextStyle st;
        st.bold = bold;
        line.runs = RichLine{TextSpan{"Hamburg", st}};
        text.lines.push_back(std::move(line));
        return text;
    };

    auto ink_at_scale = [&](bool bold, double scale) {
        QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.scale(scale, scale);
        paint_text_content(p, make_content(bold), {.faces = &faces});
        p.end();
        return count_ink(img);
    };

    const int full_plain = ink_at_scale(false, 1.0);
    const int full_bold = ink_at_scale(true, 1.0);
    const double preview_scale = 320.0 / 1920.0;
    const int prev_plain = ink_at_scale(false, preview_scale);
    const int prev_bold = ink_at_scale(true, preview_scale);
    REQUIRE(full_plain > 0);
    REQUIRE(prev_plain > 0);
    const double full_ratio = static_cast<double>(full_bold) / static_cast<double>(full_plain);
    const double prev_ratio = static_cast<double>(prev_bold) / static_cast<double>(prev_plain);
    INFO("full_ratio=" << full_ratio << " prev_ratio=" << prev_ratio);
    REQUIRE(full_ratio > 1.0);
    REQUIRE(prev_ratio > 1.0);
    // Faux bold is a design-space thicken (~3% of the font size), so the
    // preview must gain the same share of ink the output screen gains - not a
    // halo, and not a stem that vanished because it fell below a device pixel.
    // Within 15%: Qt 6.12 antialiases the small text a little differently and
    // comes to 12.3% here, where 6.8 comes to 11.9%.
    REQUIRE(std::abs(prev_ratio - full_ratio) < 0.15 * full_ratio);
}

TEST_CASE("preview-scale bold matches the downscaled output", "[render][bold][preview]")
{
    // The operator preview paints the same 1920px stage at ~1/6 scale, so bold
    // text there must weigh what the audience screen weighs after downscaling.
    // Two ways it drifted: Qt's FreeType engine emboldens a "bold" request the
    // font file does not flag as bold (a variable font's Bold named instance)
    // only on the cached-glyph path - preview sizes - and our own faux-bold
    // thicken used to be dropped as too thin to draw at preview scale.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    const double preview_scale = 320.0 / 1920.0;

    auto ink_mass = [](const QImage& img) {
        double sum = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                sum += qRed(img.pixel(x, y));
            }
        }
        return sum / 255.0;
    };

    auto text_at = [](const std::string& family, double size) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = 1900;
        text.box.height = 200;
        text.box.font_family = family;
        text.box.font_size = size;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        text.box.bold = true;
        SceneTextLine line;
        line.runs = RichLine{TextSpan{"Hamburgefonstiv 123 quick brown", {}}};
        text.lines.push_back(std::move(line));
        return text;
    };

    // Families with a real bold file, and one whose bold has to be faked.
    for (const std::string& family : {std::string("Liberation Sans"), std::string("Open Sans"),
                                     std::string("EB Garamond"), std::string("Abril Fatface")}) {
        for (double size : {36.0, 60.0, 110.0}) {
            const auto content = text_at(family, size);

            QImage full(1920, 222, QImage::Format_ARGB32_Premultiplied);
            full.fill(Qt::black);
            {
                QPainter p(&full);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::Antialiasing, true);
                paint_text_content(p, content, {.faces = &faces});
            }
            const QImage downscaled =
                full.scaled(320, 37, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            QImage preview(320, 37, QImage::Format_ARGB32_Premultiplied);
            preview.fill(Qt::black);
            {
                QPainter p(&preview);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.scale(preview_scale, preview_scale);
                paint_text_content(p, content, {.faces = &faces});
            }

            const double output_ink = ink_mass(downscaled);
            const double preview_ink = ink_mass(preview);
            REQUIRE(output_ink > 0.0);
            const double ratio = preview_ink / output_ink;
            INFO(family << " size=" << size << " output_ink=" << output_ink
                        << " preview_ink=" << preview_ink << " ratio=" << ratio);
            // Double-emboldening ran ~1.3x; a dropped faux-bold ran ~0.67x.
            REQUIRE(ratio > 0.9);
            REQUIRE(ratio < 1.12);
        }
    }
}

TEST_CASE("bold weight does not change with rendered size", "[render][bold]")
{
    // Same face, same string: ink per px^2 must not jump between a small
    // output size and a large one. Below 65 device px Qt draws cached glyph
    // rasters - where FreeType adds a second, fake bold to a variable font's
    // Bold instance and Core Text dilates every stem - and above it fills the
    // outlines. Painted the way slides paint, so what is checked is what the
    // audience sees.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    auto density = [&](const std::string& family, int px) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = px * 22;
        text.box.height = px * 3;
        text.box.font_family = family;
        text.box.font_size = px;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        text.box.bold = true;
        SceneTextLine line;
        line.runs = RichLine{TextSpan{"Hamburgefonstiv 123", {}}};
        text.lines.push_back(std::move(line));

        QImage img(px * 22, px * 3, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.setRenderHint(QPainter::Antialiasing, true);
            paint_text_content(p, text, {.faces = &faces});
        }
        double sum = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                sum += qRed(img.pixel(x, y));
            }
        }
        return sum / 255.0 / double(px) / double(px);
    };

    for (const std::string& family : {std::string("Liberation Sans"), std::string("Open Sans"),
                                     std::string("Abril Fatface")}) {
        const auto face = faces.resolve(
            seventhslide::layout::FontStyle{.family = family, .size_px = 110.0, .bold = true});
        if (face.needs_synthetic_embolden()) {
            continue;  // we thicken that one ourselves, in design space
        }
        const double small = density(family, 18);
        const double large = density(family, 110);
        INFO(family << " small=" << small << " large=" << large);
        REQUIRE(large > 0.0);
        REQUIRE(std::abs(small - large) < 0.08 * large);
    }
}

TEST_CASE("preview weight matches the downscaled output for plain text", "[render][preview]")
{
    // The regression this guards: a 20px copyright line lands on ~3 device
    // pixels in the operator preview, and a glyph raster that small is
    // grid-fit and darkened by the font engine. A CFF face like Nimbus Sans
    // came out over twice the ink the audience screen shows, so plain copyright
    // text read as bold in the preview.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    const double preview_scale = 320.0 / 1920.0;

    auto ink_mass = [](const QImage& img) {
        double sum = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                sum += qRed(img.pixel(x, y));
            }
        }
        return sum / 255.0;
    };

    auto text_at = [](const std::string& family, double size) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = 1900;
        text.box.height = 200;
        text.box.font_family = family;
        text.box.font_size = size;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        SceneTextLine line;
        line.runs =
            RichLine{TextSpan{"Words and Music by Someone (c) 1962 Sample Publishing", {}}};
        text.lines.push_back(std::move(line));
        return text;
    };

    // A family nobody has among them: it is drawn in the stand-in.
    for (const std::string& family : {std::string("Helvetica"), std::string("Liberation Sans"),
                                     std::string("Open Sans")}) {
        for (double size : {16.0, 20.0, 24.0, 40.0, 80.0}) {
            const auto content = text_at(family, size);

            QImage full(1920, 222, QImage::Format_ARGB32_Premultiplied);
            full.fill(Qt::black);
            {
                QPainter p(&full);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::Antialiasing, true);
                paint_text_content(p, content, {.faces = &faces});
            }
            const QImage downscaled =
                full.scaled(320, 37, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            QImage preview(320, 37, QImage::Format_ARGB32_Premultiplied);
            preview.fill(Qt::black);
            {
                QPainter p(&preview);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.scale(preview_scale, preview_scale);
                paint_text_content(p, content, {.faces = &faces});
            }

            const double output_ink = ink_mass(downscaled);
            const double preview_ink = ink_mass(preview);
            REQUIRE(output_ink > 0.0);
            REQUIRE(preview_ink > 0.0);
            const double ratio = preview_ink / output_ink;
            INFO(family << " size=" << size << " output_ink=" << output_ink
                        << " preview_ink=" << preview_ink << " ratio=" << ratio);
            // Outlines carry no grid fitting, so the preview runs a little
            // light where the output screen's own hinting thickens a 16px stem
            // to a whole pixel. Bolder than the output is the bug.
            REQUIRE(ratio < 1.15);
            REQUIRE(ratio > 0.65);
        }
    }
}

TEST_CASE("preview-scale text keeps its underline", "[render][preview]")
{
    // Preview text is painted as glyph outlines, and outlines carry no
    // decorations - the underline has to be drawn beside them.
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtFontFaces faces(fonts);

    auto content = [](bool underline) {
        SceneTextContent text;
        text.box.x = 0;
        text.box.y = 0;
        text.box.width = 1900;
        text.box.height = 200;
        text.box.font_size = 60;
        text.box.color = "#ffffff";
        text.box.align = "left";
        text.box.valign = "top";
        SceneTextLine line;
        TextStyle st;
        st.underline = underline;
        line.runs = RichLine{TextSpan{"Underlined", st}};
        text.lines.push_back(std::move(line));
        return text;
    };

    auto ink_at = [&](bool underline, double scale) {
        QImage img(320, 60, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(scale, scale);
        paint_text_content(p, content(underline), {.faces = &faces});
        p.end();
        return count_ink(img);
    };

    const double preview_scale = 320.0 / 1920.0;
    REQUIRE(ink_at(false, preview_scale) > 0);
    REQUIRE(ink_at(true, preview_scale) > ink_at(false, preview_scale));
}

namespace {

/// Every pixel of two images, compared.
[[nodiscard]] bool same_pixels(const QImage& a, const QImage& b)
{
    return a.size() == b.size() && a.convertToFormat(QImage::Format_ARGB32)
                                       == b.convertToFormat(QImage::Format_ARGB32);
}

[[nodiscard]] int leftmost_ink_x(const QImage& img)
{
    int left = img.width();
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < left; ++x) {
            if (!is_near_black(img.pixel(x, y))) {
                left = x;
                break;
            }
        }
    }
    return left;
}

[[nodiscard]] QImage paint_onto_black(const std::function<void(QPainter&)>& paint)
{
    QImage img(800, 400, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    paint(p);
    p.end();
    return img;
}

}  // namespace

// A frame is measured once when it is built; paint draws it exactly as it
// would have measured it itself (a clock or a hand-built frame).
TEST_CASE("a measured frame paints exactly as paint would measure it", "[render][metrics]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const auto* faces = &measurer.faces();

    SceneTextContent text;
    text.box = {30, 30, 740, 340, "Liberation Sans", 40};
    SceneTextLine styled;
    styled.runs = seventhslide::model::parse_inline_markup("Plain <b>bold</b> <i>it</i> <size=60>big</size>");
    SceneTextLine chords;
    chords.has_chords = true;
    chords.chords = parse_chord_line("[G]Amazing [D]grace");
    text.lines = {styled, chords};

    seventhslide::layout::OverlayScene overlay;
    seventhslide::layout::OverlayBox box;
    box.x = 40;
    box.y = 40;
    box.w = 300;
    box.h = 300;
    box.font_family = "Liberation Sans";
    box.font_size = 36;
    box.lines.push_back(seventhslide::model::parse_inline_markup(
        "A title long enough to wrap, with <b>bold</b>"));
    overlay.boxes.push_back(box);

    const QImage text_lazy = paint_onto_black([&](QPainter& p) { paint_text_content(p, text, {.faces = faces}); });
    const QImage overlay_lazy =
        paint_onto_black([&](QPainter& p) { paint_overlay_scene(p, overlay, {.faces = faces}); });

    seventhslide::scene::measure_text_content(text, measurer);
    overlay.boxes[0].layout = seventhslide::layout::lay_out_overlay(overlay.boxes[0], measurer);
    REQUIRE(text.lines[0].metrics.has_value());
    REQUIRE(overlay.boxes[0].layout->rows.size() > 1);

    REQUIRE(count_ink(text_lazy) > 0);
    REQUIRE(same_pixels(paint_onto_black([&](QPainter& p) { paint_text_content(p, text, {.faces = faces}); }),
                        text_lazy));
    REQUIRE(same_pixels(
        paint_onto_black([&](QPainter& p) { paint_overlay_scene(p, overlay, {.faces = faces}); }),
        overlay_lazy));
}

// Paint takes a measured frame's word for where text goes: it does not measure
// again.
TEST_CASE("paint draws text where the frame's metrics put it", "[render][metrics]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const auto* faces = &measurer.faces();

    SceneTextContent text;
    text.box = {0, 0, 800, 400, "Liberation Sans", 40};
    text.box.align = "left";
    SceneTextLine line;
    line.runs = seventhslide::model::parse_inline_markup("Hello");
    text.lines = {line};
    seventhslide::scene::measure_text_content(text, measurer);
    const int at = leftmost_ink_x(paint_onto_black([&](QPainter& p) { paint_text_content(p, text, {.faces = faces}); }));

    text.lines[0].metrics->x[0] += 200.0;
    const int moved =
        leftmost_ink_x(paint_onto_black([&](QPainter& p) { paint_text_content(p, text, {.faces = faces}); }));
    REQUIRE(moved - at == 200);

    seventhslide::layout::OverlayScene overlay;
    seventhslide::layout::OverlayBox box;
    box.x = 0;
    box.y = 0;
    box.w = 800;
    box.h = 400;
    box.font_family = "Liberation Sans";
    box.font_size = 40;
    box.text_align = "left";
    box.lines.push_back(seventhslide::model::parse_inline_markup("Hello"));
    box.layout = seventhslide::layout::lay_out_overlay(box, measurer);
    overlay.boxes.push_back(box);
    const int box_at = leftmost_ink_x(
        paint_onto_black([&](QPainter& p) { paint_overlay_scene(p, overlay, {.faces = faces}); }));
    overlay.boxes[0].layout->rows[0].runs[0].x += 150.0;
    const int box_moved = leftmost_ink_x(
        paint_onto_black([&](QPainter& p) { paint_overlay_scene(p, overlay, {.faces = faces}); }));
    REQUIRE(box_moved - box_at == 150);
}

TEST_CASE("A still's clock shows the time it is given", "[render]")
{
    seventhslide::test::ensure_qt_application();

    SceneFrame frame;
    frame.mode = SceneMode::Freeform;
    frame.canvas_width = 400;
    frame.canvas_height = 300;
    seventhslide::canvas::Object clk = seventhslide::test::scene_clock();
    clk.x = 10;
    clk.y = 10;
    clk.font_size = 28;
    clk.font_family = "Liberation Sans";
    clk.font_color = "#ffffff";
    clk.clock_24h = true;
    SceneObject clock_obj;
    clock_obj.id = "te_clock";
    clock_obj.object = clk;
    frame.objects.push_back(std::move(clock_obj));

    seventhslide::render::StillPaint still;
    still.frame = seventhslide::scene::share_frame(std::move(frame));
    still.size = QSize(400, 300);
    const auto at = [&](QTime time) {
        still.now = QDateTime(QDate(2000, 1, 1), time);
        return seventhslide::render::paint_still(still);
    };
    CHECK(at(QTime(12, 0)) == at(QTime(12, 0)));
    CHECK(at(QTime(12, 0)) != at(QTime(9, 47)));
}

TEST_CASE("A line's type paints over the box's", "[render][lines]")
{
    seventhslide::test::ensure_qt_application();
    SceneTextContent content;
    content.box.x = 0;
    content.box.y = 0;
    content.box.width = 400;
    content.box.height = 200;
    content.box.font_family = "Liberation Sans";
    content.box.font_size = 40;
    content.box.color = "#ffffff";
    content.box.align = "left";
    content.box.valign = "top";
    SceneTextLine active;
    active.runs = RichLine{TextSpan{"Active", {}}};
    active.type.color = "#ff0000";
    active.type.bold = true;
    SceneTextLine other;
    other.runs = RichLine{TextSpan{"Other", {}}};
    other.type.color = "#0000ff";
    other.type.size = 20;
    content.lines = {active, other};

    QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter painter(&img);
    paint_text_content(painter, content);
    painter.end();

    int red = 0;
    int blue = 0;
    int white = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60) {
                ++red;
            } else if (c.blue() > 200 && c.red() < 60 && c.green() < 60) {
                ++blue;
            } else if (c.red() > 200 && c.green() > 200 && c.blue() > 200) {
                ++white;
            }
        }
    }
    CHECK(red > 50);
    CHECK(blue > 20);
    CHECK(white == 0);
    // The other line is set smaller: its row is shorter than the active one.
    seventhslide::fonts::FontRegistry fonts;
    seventhslide::test::use_shipped_fonts(fonts);
    seventhslide::layout::QtTextMeasurer measurer(fonts);
    const auto a = seventhslide::scene::measure_text_line(content.lines[0], content.box, {}, measurer);
    const auto o = seventhslide::scene::measure_text_line(content.lines[1], content.box, {}, measurer);
    CHECK(o.height < a.height);
}

TEST_CASE("A turned text box turns its words", "[render][text][rotation]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::OverlayBox box;
    box.x = 150;
    box.y = 150;
    box.w = 100;
    box.h = 100;
    box.font_size = 40;
    box.font_color = "#ffffff";
    box.text_align = "left";
    box.vertical_align = "top";
    box.lines.push_back({{"MM"}});
    const auto paint = [&] {
        QImage img(400, 400, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        seventhslide::render::paint_overlay_box(p, box);
        p.end();
        return img;
    };
    const auto lit = [](const QImage& img, const QRect& area) {
        int n = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                n += qRed(img.pixel(x, y)) > 128 ? 1 : 0;
            }
        }
        return n;
    };
    // Level: along the box's top; none of it down the right side.
    const QImage level = paint();
    CHECK(lit(level, QRect(150, 150, 100, 50)) > 0);
    CHECK(lit(level, QRect(200, 205, 50, 45)) == 0);
    // A quarter turn clockwise: the top row is now down the right side.
    box.rotation = 90;
    const QImage turned = paint();
    CHECK(lit(turned, QRect(200, 150, 50, 100)) > 0);
    CHECK(lit(turned, QRect(150, 150, 45, 100)) == 0);
}
