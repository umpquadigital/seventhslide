#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include "../src/app/freeform_shape_item.hpp"

#include <seventhslide/canvas/object.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/geom/shape_path.hpp>
#include <seventhslide/render/css_color.hpp>
#include <seventhslide/render/shape_painter.hpp>

#include <nlohmann/json.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <utility>

using Catch::Approx;
using seventhslide::geom::ShapeAdjust;
using seventhslide::geom::ShapeKind;
using seventhslide::geom::build_shape_path;
using seventhslide::geom::clamp_adjust;
using seventhslide::geom::shape_kind_from_string;
using seventhslide::render::css_color;
using seventhslide::render::shape_painter_path;

namespace {

/// Every on-path vertex (control points excluded - they may sit outside a
/// convex hull without the drawn outline leaving the box).
[[nodiscard]] std::vector<seventhslide::geom::ShapePoint> vertices(
    const seventhslide::geom::ShapePath& path)
{
    std::vector<seventhslide::geom::ShapePoint> out;
    for (const auto& sub : path.subpaths) {
        out.push_back(sub.start);
        for (const auto& seg : sub.segments) {
            out.push_back(seg.to);
        }
    }
    return out;
}

[[nodiscard]] int segment_count(const seventhslide::geom::ShapePath& path)
{
    int n = 0;
    for (const auto& sub : path.subpaths) {
        n += static_cast<int>(sub.segments.size());
    }
    return n;
}

} // namespace

TEST_CASE("shape kind ids round-trip", "[geom][shape]")
{
    for (const auto kind : seventhslide::geom::all_shape_kinds()) {
        REQUIRE(shape_kind_from_string(seventhslide::geom::to_string(kind)) == kind);
    }
    // Unknown ids degrade to the safest outline rather than throwing.
    REQUIRE(shape_kind_from_string("nonesuch") == ShapeKind::Rectangle);
    REQUIRE(shape_kind_from_string("") == ShapeKind::Rectangle);
}

TEST_CASE("adjustments are clamped into each kind's window", "[geom][shape]")
{
    ShapeAdjust wild;
    wild.corner = 9.0;
    wild.points = 200;
    wild.inner = -3.0;
    wild.head = 5.0;
    wild.thickness = 0.0;

    const auto rounded = clamp_adjust(ShapeKind::RoundedRect, wild);
    REQUIRE(rounded.corner == Approx(0.5));
    REQUIRE(rounded.points == 24);
    REQUIRE(rounded.inner == Approx(0.05));

    // A double arrow spends its head budget twice, so each head caps at half.
    REQUIRE(clamp_adjust(ShapeKind::Arrow, wild).head == Approx(1.0));
    REQUIRE(clamp_adjust(ShapeKind::DoubleArrow, wild).head == Approx(0.5));
    REQUIRE(clamp_adjust(ShapeKind::Arrow, wild).thickness == Approx(0.05));
}

TEST_CASE("every outline stays inside its box", "[geom][shape]")
{
    const ShapeAdjust adjust;
    for (const auto kind : seventhslide::geom::all_shape_kinds()) {
        const auto path = build_shape_path(kind, 640, 200, adjust);
        REQUIRE_FALSE(path.empty());
        for (const auto& pt : vertices(path)) {
            REQUIRE(pt.x >= Approx(0.0).margin(1e-9));
            REQUIRE(pt.x <= Approx(640.0).margin(1e-9));
            REQUIRE(pt.y >= Approx(0.0).margin(1e-9));
            REQUIRE(pt.y <= Approx(200.0).margin(1e-9));
        }
    }
}

TEST_CASE("outlines reflow with the box instead of translating", "[geom][shape]")
{
    // Doubling the box must double every coordinate: that is what makes a shape
    // "reflow" on resize the way ProPresenter / PowerPoint shapes do.
    const ShapeAdjust adjust;
    for (const auto kind : seventhslide::geom::all_shape_kinds()) {
        const auto small = vertices(build_shape_path(kind, 100, 60, adjust));
        const auto large = vertices(build_shape_path(kind, 200, 120, adjust));
        REQUIRE(small.size() == large.size());
        for (std::size_t i = 0; i < small.size(); ++i) {
            REQUIRE(large[i].x == Approx(small[i].x * 2.0).margin(1e-9));
            REQUIRE(large[i].y == Approx(small[i].y * 2.0).margin(1e-9));
        }
    }
}

TEST_CASE("rounded corners stay circular in a lopsided box", "[geom][shape]")
{
    ShapeAdjust adjust;
    adjust.corner = 0.25;
    const auto path = build_shape_path(ShapeKind::RoundedRect, 800, 100, adjust);
    // radius = corner * min(w, h) = 25 on both axes, not 200 x 25.
    const auto pts = vertices(path);
    REQUIRE(pts.front().x == Approx(25.0));
    REQUIRE(pts.front().y == Approx(0.0));
    // Four straight edges + four corner arcs.
    REQUIRE(segment_count(path) == 8);
}

TEST_CASE("zero corner radius degenerates to a plain rectangle", "[geom][shape]")
{
    ShapeAdjust adjust;
    adjust.corner = 0.0;
    const auto rounded = build_shape_path(ShapeKind::RoundedRect, 120, 80, adjust);
    const auto plain = build_shape_path(ShapeKind::Rectangle, 120, 80, adjust);
    REQUIRE(rounded == plain);
}

TEST_CASE("polygon and star vertex counts follow the points adjustment", "[geom][shape]")
{
    ShapeAdjust adjust;
    adjust.points = 6;
    REQUIRE(vertices(build_shape_path(ShapeKind::Polygon, 200, 200, adjust)).size() == 6);
    // A star alternates outer and inner vertices.
    REQUIRE(vertices(build_shape_path(ShapeKind::Star, 200, 200, adjust)).size() == 12);

    const auto star = vertices(build_shape_path(ShapeKind::Star, 200, 200, adjust));
    const double cx = 100.0;
    const double cy = 100.0;
    const auto radius = [&](std::size_t i) {
        return std::hypot(star[i].x - cx, star[i].y - cy);
    };
    REQUIRE(radius(1) < radius(0));
    REQUIRE(radius(1) == Approx(radius(0) * adjust.inner));
}

TEST_CASE("arrow shaft and head track their adjustments", "[geom][shape]")
{
    ShapeAdjust adjust;
    adjust.thickness = 0.5;
    adjust.head = 0.25;
    const auto pts = vertices(build_shape_path(ShapeKind::Arrow, 400, 200, adjust));
    REQUIRE(pts.size() == 7);
    // Shaft is centred: 50% of 200 -> y 50..150.
    REQUIRE(pts[0].y == Approx(50.0));
    REQUIRE(pts.back().y == Approx(150.0));
    // Neck sits one head-length in from the tip: 400 - 0.25 * 400.
    REQUIRE(pts[1].x == Approx(300.0));
    // The tip is the only point on the right edge, vertically centred.
    REQUIRE(pts[3].x == Approx(400.0));
    REQUIRE(pts[3].y == Approx(100.0));
}

TEST_CASE("double arrow heads do not overlap at max adjustment", "[geom][shape]")
{
    ShapeAdjust adjust;
    adjust.head = 5.0; // clamped to 0.5
    const auto pts = vertices(build_shape_path(ShapeKind::DoubleArrow, 400, 200, adjust));
    REQUIRE(pts.size() == 10);
    REQUIRE(pts[1].x == Approx(200.0));
    REQUIRE(pts[4].x == Approx(200.0));
}

TEST_CASE("degenerate boxes produce no outline", "[geom][shape]")
{
    REQUIRE(build_shape_path(ShapeKind::Ellipse, 0, 100, {}).empty());
    REQUIRE(build_shape_path(ShapeKind::Ellipse, 100, -4, {}).empty());
}

TEST_CASE("css_color parses functional notation", "[render][color]")
{
    // The Freeform default fill has always been rgba(); QColor alone rejects it,
    // which silently painted nothing.
    const QColor rgba = css_color("rgba(255,255,255,0.08)");
    REQUIRE(rgba.isValid());
    REQUIRE(rgba.red() == 255);
    REQUIRE(rgba.alpha() == 20);

    REQUIRE(css_color("rgb(10, 20, 30)") == QColor(10, 20, 30));
    REQUIRE(css_color("rgb(0% 50% 100%)") == QColor(0, 128, 255));
    REQUIRE(css_color("rgb(10 20 30 / 50%)").alpha() == 128);
    REQUIRE(css_color("#14ffffff").alpha() == 20);
    REQUIRE(css_color("#ff0000") == QColor(255, 0, 0));
    REQUIRE(css_color("white") == QColor(Qt::white));
    REQUIRE(seventhslide::render::css_color_hex("red") == "#ff0000");
    REQUIRE(seventhslide::render::css_color_hex("rgb(255, 0, 0)") == "#ff0000");
    REQUIRE(seventhslide::render::css_color_hex("#ABC") == "#aabbcc");
    REQUIRE(seventhslide::render::css_color_hex("rgba(255,0,0,0.5)") == "#80ff0000");
    REQUIRE(seventhslide::render::css_color_hex("not-a-color").empty());

    REQUIRE_FALSE(css_color("").isValid());
    REQUIRE_FALSE(css_color("none").isValid());
    REQUIRE_FALSE(css_color("rgb(1,2)").isValid());
    REQUIRE_FALSE(css_color("not-a-color").isValid());
}

TEST_CASE("shape painter fills the outline and leaves the outside alone", "[render][shape]")
{
    seventhslide::test::ensure_qt_application();

    QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        seventhslide::canvas::Fill fill;
        fill.color = "#ff0000";
        seventhslide::render::paint_shape_outline(painter, ShapeKind::Ellipse, 100, 100, {}, fill,
                                                  seventhslide::canvas::Line{});
    }
    // Centre is inside the ellipse; the corner is not.
    REQUIRE(qRed(img.pixel(50, 50)) > 200);
    REQUIRE(qRed(img.pixel(2, 2)) < 40);
}

TEST_CASE("A fill of kind none paints only the line, and a dashed line leaves gaps",
          "[render][shape][fill][line]")
{
    seventhslide::test::ensure_qt_application();
    using seventhslide::canvas::Fill;
    using seventhslide::canvas::FillKind;
    using seventhslide::canvas::Line;
    using seventhslide::canvas::LineDash;

    const auto paint = [](const Fill& fill, const Line& line) {
        QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        // The rectangle is inset so the whole line lies on the image.
        painter.translate(10, 10);
        seventhslide::render::paint_shape_outline(painter, ShapeKind::Rectangle, 180, 80, {}, fill,
                                                  line);
        return img;
    };
    const auto red_along_top = [](const QImage& img) {
        int lit = 0;
        int dark = 0;
        for (int x = 20; x < 180; ++x) {
            (qRed(img.pixel(x, 10)) > 128 ? lit : dark) += 1;
        }
        return std::pair{lit, dark};
    };

    Fill fill;
    fill.color = "#0000ff";
    Line line;
    line.color = "#ff0000";
    line.width = 4;

    SECTION("solid fill and solid line")
    {
        const QImage img = paint(fill, line);
        CHECK(qBlue(img.pixel(100, 50)) > 200);
        const auto [lit, dark] = red_along_top(img);
        CHECK(lit == 160);
        CHECK(dark == 0);
    }
    SECTION("the fill's kind none keeps its colour but paints nothing")
    {
        fill.kind = FillKind::None;
        const QImage img = paint(fill, line);
        CHECK(qBlue(img.pixel(100, 50)) < 40);
        CHECK(red_along_top(img).first == 160);
    }
    SECTION("a line with no colour or no width paints nothing")
    {
        line.color.clear();
        CHECK(red_along_top(paint(fill, line)).first == 0);
        line.color = "#ff0000";
        line.width = 0;
        CHECK(red_along_top(paint(fill, line)).first == 0);
    }
    SECTION("each dash leaves gaps along the edge, a dotted line the most")
    {
        line.dash = LineDash::Dash;
        const auto dashed = red_along_top(paint(fill, line));
        CHECK(dashed.first > 40);
        CHECK(dashed.second > 20);
        line.dash = LineDash::Dot;
        const auto dotted = red_along_top(paint(fill, line));
        CHECK(dotted.first > 20);
        CHECK(dotted.second > dashed.second);
        line.dash = LineDash::DashDot;
        const auto mixed = red_along_top(paint(fill, line));
        CHECK(mixed.first > 40);
        CHECK(mixed.second > 20);
    }
    SECTION("the pen's dash is in widths, so the stage's zoom keeps the pattern")
    {
        line.dash = LineDash::Dash;
        const QPen one = seventhslide::render::line_pen(line, 1.0);
        const QPen two = seventhslide::render::line_pen(line, 2.0);
        CHECK(one.style() == Qt::DashLine);
        CHECK(two.widthF() == Approx(8.0));
        CHECK(two.dashPattern() == one.dashPattern());
    }
}

TEST_CASE("shape_painter_path matches the core outline", "[render][shape]")
{
    seventhslide::test::ensure_qt_application();
    const QPainterPath path = shape_painter_path(ShapeKind::Triangle, 100, 50, {});
    REQUIRE_FALSE(path.isEmpty());
    const QRectF bounds = path.boundingRect();
    REQUIRE(bounds.left() == Approx(0.0));
    REQUIRE(bounds.top() == Approx(0.0));
    REQUIRE(bounds.width() == Approx(100.0));
    REQUIRE(bounds.height() == Approx(50.0));
}

// --- Editor canvas item ------------------------------------------------------
// The stage must paint through the same geometry as the audience output; these
// pin the property wiring that makes that true.

TEST_CASE("FreeformShapeItem paints the selected kind", "[app][shape]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::app::FreeformShapeItem item;
    item.setWidth(100);
    item.setHeight(100);
    item.setKind(QStringLiteral("ellipse"));
    item.setFill(QStringLiteral("rgba(255,0,0,1)"));
    item.setStrokeWidth(0);

    QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        item.paint(&painter);
    }
    REQUIRE(qRed(img.pixel(50, 50)) > 200);
    // Outside the ellipse, inside the box.
    REQUIRE(qRed(img.pixel(2, 2)) < 40);

    // A rectangle of the same box covers the corner the ellipse missed.
    item.setKind(QStringLiteral("rectangle"));
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        item.paint(&painter);
    }
    REQUIRE(qRed(img.pixel(2, 2)) > 200);
}

TEST_CASE("FreeformShapeItem takes the fill's kind and the line's dash", "[app][shape]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::app::FreeformShapeItem item;
    item.setWidth(200);
    item.setHeight(100);
    item.setKind(QStringLiteral("rectangle"));
    item.setFill(QStringLiteral("#0000ff"));
    item.setStroke(QStringLiteral("#ff0000"));
    item.setStrokeWidth(6);
    item.setPadding(10);

    const auto painted = [&item] {
        QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter painter(&img);
        item.paint(&painter);
        return img;
    };
    const auto red_along_top = [](const QImage& img) {
        int lit = 0;
        for (int x = 20; x < 180; ++x) {
            if (qRed(img.pixel(x, 10)) > 128) {
                ++lit;
            }
        }
        return lit;
    };

    CHECK(qBlue(painted().pixel(100, 50)) > 200);
    CHECK(red_along_top(painted()) == 160);

    // None: the colour stays on the item, nothing fills.
    item.setFillKind(QStringLiteral("none"));
    CHECK(qBlue(painted().pixel(100, 50)) < 40);
    CHECK(item.fill() == QStringLiteral("#0000ff"));

    item.setDash(QStringLiteral("dash"));
    const int dashed = red_along_top(painted());
    CHECK(dashed > 40);
    CHECK(dashed < 150);
}

TEST_CASE("FreeformShapeItem insets the shape box by its padding", "[app][shape]")
{
    seventhslide::test::ensure_qt_application();

    seventhslide::app::FreeformShapeItem item;
    item.setWidth(120);
    item.setHeight(120);
    item.setKind(QStringLiteral("rectangle"));
    item.setFill(QStringLiteral("#ff0000"));
    item.setStrokeWidth(0);
    item.setPadding(10);

    QImage img(120, 120, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    {
        QPainter painter(&img);
        item.paint(&painter);
    }
    // The padding band stays background; the inset box is filled.
    REQUIRE(qRed(img.pixel(4, 4)) < 40);
    REQUIRE(qRed(img.pixel(60, 60)) > 200);
    REQUIRE(qRed(img.pixel(115, 115)) < 40);
}

TEST_CASE("FreeformShapeItem scales the border with the stage zoom", "[app][shape]")
{
    seventhslide::test::ensure_qt_application();

    const auto border_thickness = [](qreal scale) {
        seventhslide::app::FreeformShapeItem item;
        item.setWidth(200);
        item.setHeight(200);
        item.setKind(QStringLiteral("rectangle"));
        item.setFill(QStringLiteral("none"));
        item.setStroke(QStringLiteral("#ff0000"));
        item.setStrokeWidth(20);
        item.setScaleFactor(scale);

        QImage img(200, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        {
            QPainter painter(&img);
            item.paint(&painter);
        }
        // Walk in from the mid-left edge until the border ends.
        int px = 0;
        for (int x = 0; x < 100; ++x) {
            if (qRed(img.pixel(x, 100)) > 128) {
                ++px;
            }
        }
        return px;
    };

    // A 20px design border centred on the edge shows half its width inside the
    // box, so half-scale must halve what is visible.
    const int full = border_thickness(1.0);
    const int half = border_thickness(0.5);
    REQUIRE(full > half);
    REQUIRE(half == Approx(full / 2.0).margin(2));
}

TEST_CASE("A cloud callout is a cloud and three bubbles", "[geom][shape][drawn]")
{
    const auto path = build_shape_path(ShapeKind::Cloud, 400, 300, {});
    REQUIRE(path.subpaths.size() == 4);
    CHECK(path.subpaths.front().segments.size() == 10);
    for (const auto& sub : path.subpaths) {
        CHECK(sub.closed);
    }
}

TEST_CASE("A drawn outline goes through its points, curved where they are smooth",
          "[geom][shape][drawn]")
{
    using seventhslide::geom::PathPoint;
    using seventhslide::geom::SegmentKind;
    ShapeAdjust adjust;
    adjust.path = {{0, 0}, {1, 0}, {1, 1}};
    // Open: two straight segments, its corners where they were put.
    auto path = build_shape_path(ShapeKind::Drawn, 200, 100, adjust);
    REQUIRE(path.subpaths.size() == 1);
    CHECK_FALSE(path.subpaths.front().closed);
    REQUIRE(path.subpaths.front().segments.size() == 2);
    CHECK(path.subpaths.front().segments[1].to.x == 200);
    CHECK(path.subpaths.front().segments[1].to.y == 100);
    CHECK(path.subpaths.front().segments[0].kind == SegmentKind::Line);
    CHECK(seventhslide::geom::is_open(ShapeKind::Drawn, adjust));
    // Closed: back to the first.
    adjust.closed = true;
    path = build_shape_path(ShapeKind::Drawn, 200, 100, adjust);
    CHECK(path.subpaths.front().closed);
    CHECK(path.subpaths.front().segments.size() == 3);
    CHECK_FALSE(seventhslide::geom::is_open(ShapeKind::Drawn, adjust));
    // Smooth: cubics through the same points.
    for (auto& pt : adjust.path) {
        pt.smooth = true;
    }
    path = build_shape_path(ShapeKind::Drawn, 200, 100, adjust);
    for (const auto& seg : path.subpaths.front().segments) {
        CHECK(seg.kind == SegmentKind::Cubic);
    }
    // One point draws nothing.
    adjust.path = {{0.5, 0.5}};
    CHECK(build_shape_path(ShapeKind::Drawn, 200, 100, adjust).empty());
}

TEST_CASE("A drawn shape keeps its points through its stored form", "[geom][shape][drawn][json]")
{
    seventhslide::canvas::Object obj;
    obj.type = seventhslide::canvas::ObjectType::Shape;
    obj.shape_kind = ShapeKind::Drawn;
    obj.shape_adjust.path = {{0, 0.25}, {0.5, 1, true}, {1, 0}};
    obj.line.end = seventhslide::canvas::LineEnd::Arrow;
    const nlohmann::json j = obj;
    CHECK(j.at("shape_path").size() == 3);
    CHECK_FALSE(j.contains("shape_closed"));
    const auto back = j.get<seventhslide::canvas::Object>();
    CHECK(back.shape_kind == ShapeKind::Drawn);
    CHECK(back.shape_adjust.path == obj.shape_adjust.path);
    CHECK(back.line.end == seventhslide::canvas::LineEnd::Arrow);
}

TEST_CASE("An open outline paints no fill, and its end an arrow", "[render][shape][drawn]")
{
    seventhslide::test::ensure_qt_application();
    QImage img(200, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    ShapeAdjust adjust;
    // A V from the top left, down to the middle, up to the top right.
    adjust.path = {{0, 0}, {0.5, 1}, {1, 0}};
    seventhslide::canvas::Fill fill;
    fill.color = "#ff0000";
    seventhslide::canvas::Line line;
    line.color = "#00ff00";
    line.width = 4;
    line.end = seventhslide::canvas::LineEnd::Arrow;
    {
        QPainter painter(&img);
        seventhslide::render::paint_shape_outline(painter, ShapeKind::Drawn, 200, 100, adjust, fill,
                                                  line);
    }
    // Inside the V: no fill.
    CHECK(qRed(img.pixel(100, 30)) < 40);
    // On the line, and the arrowhead wider than the line at its end: 14 px
    // back from the tip and 4 px off the line is the head, not the line.
    CHECK(qGreen(img.pixel(50, 50)) > 150);
    CHECK(qGreen(img.pixel(193, 13)) > 100);
    // Its start has none.
    CHECK(qGreen(img.pixel(7, 13)) < 40);
}

TEST_CASE("A drawn shape's box fits its curve", "[geom][shape][drawn]")
{
    auto doc = seventhslide::freeform::make_document("Curve", {"Main"});
    std::vector<seventhslide::geom::PathPoint> points = {
        {100, 100, true}, {110, 300, true}, {500, 300, true}, {510, 100, true}};
    const auto obj = seventhslide::freeform::make_drawn_object(doc, points, false);
    // The curve bows past its points; the box goes with it.
    CHECK(obj.y + obj.h > 305);
    for (const auto& pt : vertices(build_shape_path(ShapeKind::Drawn, obj.w, obj.h, obj.shape_adjust))) {
        CHECK(pt.y >= Approx(0.0).margin(1e-6));
        CHECK(pt.y <= Approx(obj.h).margin(1e-6));
    }
    // The points stay where they were put.
    CHECK(obj.y + obj.shape_adjust.path[1].y * obj.h == Approx(300.0));
    CHECK(obj.x + obj.shape_adjust.path[0].x * obj.w == Approx(100.0));
    // A straight line has a box too.
    const auto line = seventhslide::freeform::make_drawn_object(doc, {{10, 50}, {210, 50}}, false);
    CHECK(line.h == Approx(8.0));
    CHECK(line.y == Approx(46.0));
    CHECK(line.shape_adjust.path[0].y == Approx(0.5));
}
