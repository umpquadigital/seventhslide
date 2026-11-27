// Shadows, glows, soft edges, text outlines and text shadows: derived from
// the element's own pixels, in device pixels, so they look the same live,
// retained and on the stage.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/canvas/parts.hpp>
#include <seventhslide/layout/overlays.hpp>
#include <seventhslide/model/rich_text.hpp>
#include <seventhslide/render/effects_painter.hpp>
#include <seventhslide/render/picture_painter.hpp>
#include <seventhslide/render/text_painter.hpp>

#include <QColor>
#include <QImage>
#include <QPainter>

#include <cstdint>

using seventhslide::canvas::Effects;
using seventhslide::render::apply_soft_edge;
using seventhslide::render::blurred_coverage;
using seventhslide::render::coverage_of;
using seventhslide::render::paint_with_effects;
using seventhslide::render::shrunk_to_show;
using seventhslide::render::tinted_coverage;

namespace {

/// A white square of ``side`` in the middle of a transparent ``size`` image.
QImage square(int size, int side)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const int at = (size - side) / 2;
    p.fillRect(at, at, side, side, Qt::white);
    return img;
}

/// A grey image's total.
std::uint64_t grey_sum(const QImage& img)
{
    std::uint64_t sum = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            sum += qGray(img.pixel(x, y));
        }
    }
    return sum;
}

int grey(const QImage& img, int x, int y) { return qGray(img.pixel(x, y)); }

/// Paint a white rectangle at (60, 60, 80, 80) on black, with ``effects``.
QImage painted(const Effects& effects, double turn = 0.0)
{
    QImage img(200, 200, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    if (turn != 0.0) {
        p.translate(100, 100);
        p.rotate(turn);
        p.translate(-100, -100);
    }
    paint_with_effects(p, effects, QRectF(60, 60, 80, 80),
                       [](QPainter& q) { q.fillRect(QRectF(60, 60, 80, 80), Qt::white); });
    return img;
}

bool is_red(QRgb px) { return qRed(px) > 150 && qGreen(px) < 80 && qBlue(px) < 80; }
bool is_green(QRgb px) { return qGreen(px) > 100 && qRed(px) < 80 && qBlue(px) < 80; }
bool is_blue(QRgb px) { return qBlue(px) > 100 && qRed(px) < 80 && qGreen(px) < 80; }
bool is_black(QRgb px) { return qRed(px) < 20 && qGreen(px) < 20 && qBlue(px) < 20; }
bool is_white(QRgb px) { return qRed(px) > 235 && qGreen(px) > 235 && qBlue(px) > 235; }

}  // namespace

TEST_CASE("A blur spreads coverage past the edge and keeps the total", "[render][effects]")
{
    seventhslide::test::ensure_qt_application();
    const QImage src = coverage_of(square(80, 20));  // 30..50
    CHECK(src.format() == QImage::Format_Grayscale8);
    CHECK(grey(src, 40, 40) == 255);
    CHECK(grey(src, 10, 10) == 0);
    const QImage soft = blurred_coverage(src, 6);
    CHECK(grey(soft, 40, 40) > 200);
    // Just outside the square: some; far away: none.
    CHECK(grey(soft, 53, 40) > 0);
    CHECK(grey(soft, 53, 40) < 255);
    CHECK(grey(soft, 70, 40) == 0);
    const auto before = grey_sum(src);
    const auto after = grey_sum(soft);
    CHECK(after > before * 97 / 100);
    CHECK(after < before * 103 / 100);
    // Under half a pixel is no blur at all.
    CHECK(blurred_coverage(src, 0.2) == src);
}

TEST_CASE("Tinted coverage is the shape in one colour, and a soft edge fades inward",
          "[render][effects]")
{
    seventhslide::test::ensure_qt_application();
    const QImage body = square(80, 40);  // 20..60
    const QImage src = coverage_of(body);
    const QImage red = tinted_coverage(src, QColor(255, 0, 0, 128));
    const QRgb inside = red.pixel(40, 40);
    CHECK(qAlpha(inside) == 128);
    CHECK(qRed(inside) == 128);  // premultiplied
    CHECK(qGreen(inside) == 0);
    CHECK(qAlpha(red.pixel(5, 5)) == 0);
    // Lifted coverage: a faint edge becomes solid.
    const QImage lifted = tinted_coverage(blurred_coverage(src, 6), QColor(0, 255, 0), 2.5);
    CHECK(qAlpha(lifted.pixel(40, 40)) == 255);

    QImage faded = body;
    apply_soft_edge(faded, 8);
    CHECK(qAlpha(faded.pixel(40, 40)) == 255);
    CHECK(qAlpha(faded.pixel(21, 40)) < 128);
    CHECK(qAlpha(faded.pixel(21, 40)) < qAlpha(faded.pixel(26, 40)));
}

TEST_CASE("A shadow falls at its angle on the slide, whatever the body's turn",
          "[render][effects][shadow]")
{
    seventhslide::test::ensure_qt_application();
    Effects fx;
    fx.shadow.on = true;
    fx.shadow.color = "#ff0000";
    fx.shadow.blur = 0;
    fx.shadow.distance = 20;
    fx.shadow.angle = 0;  // to the right

    const QImage right = painted(fx);
    CHECK(is_white(right.pixel(100, 100)));
    CHECK(is_red(right.pixel(150, 100)));
    CHECK(is_black(right.pixel(50, 100)));
    CHECK(is_black(right.pixel(100, 150)));

    fx.shadow.angle = 90;  // straight down
    const QImage down = painted(fx);
    CHECK(is_red(down.pixel(100, 150)));
    CHECK(is_black(down.pixel(150, 100)));

    // The body turned half round: its shadow still falls down the slide.
    const QImage turned = painted(fx, 180);
    CHECK(is_red(turned.pixel(100, 150)));
    CHECK(is_black(turned.pixel(100, 50)));

    // With no effect on, the body is drawn as it is.
    const QImage plain = painted(Effects{});
    CHECK(is_white(plain.pixel(100, 100)));
    CHECK(is_black(plain.pixel(150, 100)));
}

TEST_CASE("A glow surrounds the body and a soft edge fades it", "[render][effects][glow]")
{
    seventhslide::test::ensure_qt_application();
    Effects fx;
    fx.glow.on = true;
    fx.glow.color = "#00ff00";
    fx.glow.size = 10;
    const QImage glow = painted(fx);
    CHECK(is_white(glow.pixel(100, 100)));
    CHECK(is_green(glow.pixel(144, 100)));
    CHECK(is_green(glow.pixel(100, 56)));
    CHECK(is_black(glow.pixel(185, 100)));

    Effects soft;
    soft.soft_edge = 10;
    const QImage faded = painted(soft);
    CHECK(is_white(faded.pixel(100, 100)));
    CHECK(qRed(faded.pixel(62, 100)) < 128);
    CHECK(qRed(faded.pixel(62, 100)) < qRed(faded.pixel(70, 100)));
}

TEST_CASE("A text outline paints round the glyphs and a text shadow under them",
          "[render][effects][text]")
{
    seventhslide::test::ensure_qt_application();
    seventhslide::layout::OverlayBox box;
    box.x = 20;
    box.y = 20;
    box.w = 360;
    box.h = 160;
    box.font_size = 100;
    box.font_color = "#ffffff";
    box.bold = true;
    box.lines.push_back({seventhslide::model::TextSpan{"HI", {}}});

    const auto count = [&box](auto pred) {
        QImage img(400, 200, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        seventhslide::render::paint_overlay_box(p, box);
        int n = 0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (pred(img.pixel(x, y))) {
                    ++n;
                }
            }
        }
        return n;
    };

    CHECK(count(is_white) > 200);
    CHECK(count(is_red) == 0);
    CHECK(count(is_blue) == 0);

    box.effects.text_outline.color = "#ff0000";
    box.effects.text_outline.width = 6;
    CHECK(count(is_red) > 200);
    CHECK(count(is_white) > 200);

    box.effects.text_outline.width = 0;
    box.effects.text_shadow.on = true;
    box.effects.text_shadow.color = "#0000ff";
    box.effects.text_shadow.blur = 0;
    box.effects.text_shadow.distance = 12;
    box.effects.text_shadow.angle = 45;
    CHECK(count(is_blue) > 200);
    CHECK(count(is_red) == 0);
}

TEST_CASE("The effects' reach says how far past the box they paint", "[render][effects]")
{
    Effects fx;
    CHECK(seventhslide::canvas::effects_reach(fx) == 0.0);
    fx.shadow.on = true;
    fx.shadow.blur = 8;
    fx.shadow.distance = 6;
    CHECK(seventhslide::canvas::effects_reach(fx) == 22.0);
    fx.glow.on = true;
    fx.glow.size = 20;
    CHECK(seventhslide::canvas::effects_reach(fx) == 40.0);
    // A soft edge fades inward: no reach of its own.
    Effects soft;
    soft.soft_edge = 30;
    CHECK(seventhslide::canvas::effects_reach(soft) == 0.0);
}

TEST_CASE("A picture shrunk for a preview keeps its shape and never grows", "[render][picture]")
{
    QImage big(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    big.fill(Qt::white);
    const QImage small = shrunk_to_show(big, 480, 200);
    CHECK(small.width() == 480);
    CHECK(small.height() == 270);
    CHECK(shrunk_to_show(big, 1600, 900).size() == big.size());
    CHECK(shrunk_to_show(big, 3840, 2160).size() == big.size());
    CHECK(shrunk_to_show(QImage(), 100, 100).isNull());
}

