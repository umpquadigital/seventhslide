// DeckLink input (step 5d): the card's 8-bit YUV as RGB, in the right matrix
// for HD and SD; and, on a machine without Desktop Video (as this one is),
// no inputs listed and a receiver that says why it has none.

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/deinterlace.hpp>

#include <QColor>

#include <cstdint>
#include <cstdlib>
#include <vector>

using seventhslide::render::bmd_uyvy_to_image;

namespace {

/// ``width`` x ``height`` of one 4:2:2 colour (U Y V Y), rows padded.
std::vector<std::uint8_t> uyvy(int width, int height, int row_bytes, std::uint8_t y, std::uint8_t u,
                               std::uint8_t v)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(row_bytes) * height, 0);
    for (int row = 0; row < height; ++row) {
        auto* p = out.data() + static_cast<std::ptrdiff_t>(row) * row_bytes;
        for (int x = 0; x < width; x += 2, p += 4) {
            p[0] = u;
            p[1] = y;
            p[2] = v;
            p[3] = y;
        }
    }
    return out;
}

bool near(const QColor& c, int r, int g, int b)
{
    return std::abs(c.red() - r) <= 3 && std::abs(c.green() - g) <= 3 && std::abs(c.blue() - b) <= 3;
}

} // namespace

TEST_CASE("A DeckLink input's YUV becomes RGB in its standard's colours", "[render][bmd][live]")
{
    // HD (BT.709), video range: white, black, and red.
    {
        const auto white = uyvy(1280, 720, 1280 * 2 + 64, 235, 128, 128);
        const QImage out = bmd_uyvy_to_image(white.data(), 1280, 720, 1280 * 2 + 64);
        REQUIRE(out.size() == QSize(1280, 720));
        CHECK(near(out.pixelColor(640, 360), 255, 255, 255));
        const auto black = uyvy(1280, 720, 1280 * 2, 16, 128, 128);
        CHECK(near(bmd_uyvy_to_image(black.data(), 1280, 720, 1280 * 2).pixelColor(1, 1), 0, 0, 0));
        const auto red = uyvy(1280, 720, 1280 * 2, 63, 102, 240);
        CHECK(near(bmd_uyvy_to_image(red.data(), 1280, 720, 1280 * 2).pixelColor(9, 9), 255, 0, 0));
    }
    // SD (BT.601): red has other numbers.
    {
        const auto red = uyvy(720, 486, 720 * 2, 81, 90, 240);
        CHECK(near(bmd_uyvy_to_image(red.data(), 720, 486, 720 * 2).pixelColor(9, 9), 255, 0, 0));
    }
    // An odd width keeps its last column; nonsense is no picture.
    {
        const auto grey = uyvy(7, 2, 16, 126, 128, 128);
        const QImage out = bmd_uyvy_to_image(grey.data(), 7, 2, 16);
        REQUIRE(out.width() == 7);
        CHECK(out.pixelColor(6, 1) == out.pixelColor(0, 0));
        CHECK(bmd_uyvy_to_image(nullptr, 7, 2, 16).isNull());
        CHECK(bmd_uyvy_to_image(grey.data(), 7, 2, 4).isNull());
    }
}

TEST_CASE("Without Desktop Video there are no DeckLink inputs, and a receiver says why",
          "[render][bmd][live]")
{
    seventhslide::test::ensure_qt_application();
    if (seventhslide::render::bmd_runtime_available()) {
        SKIP("this machine has Desktop Video");
    }
    CHECK(seventhslide::render::bmd_list_input_devices().empty());
    seventhslide::render::BmdReceiver receiver(QStringLiteral("DeckLink Mini Recorder"), {});
    CHECK_FALSE(receiver.available());
    CHECK(receiver.frame().isNull());
    CHECK(receiver.problem() == seventhslide::render::bmd_missing_message());
}

TEST_CASE("An interlaced picture is made whole from its upper field", "[render][live]")
{
    // Fields caught apart: white on the even lines, black on the odd.
    QImage picture(4, 5, QImage::Format_RGB32);
    for (int y = 0; y < picture.height(); ++y) {
        const QRgb shade = y % 2 == 0 ? qRgb(200, 100, 50) : qRgb(0, 0, 0);
        for (int x = 0; x < picture.width(); ++x) {
            picture.setPixel(x, y, shade);
        }
    }
    picture.setPixel(1, 2, qRgb(100, 50, 250));
    seventhslide::render::deinterlace(picture);
    // The upper field kept; the lower made from it, each line the mean of
    // its neighbours (the last its one above).
    CHECK(picture.pixel(0, 0) == qRgb(200, 100, 50));
    CHECK(picture.pixel(0, 1) == qRgb(200, 100, 50));
    CHECK(picture.pixel(1, 1) == qRgb(150, 75, 150));
    CHECK(picture.pixel(1, 2) == qRgb(100, 50, 250));
    CHECK(picture.pixel(1, 3) == qRgb(150, 75, 150));
    CHECK(picture.pixel(3, 4) == qRgb(200, 100, 50));
    CHECK(qAlpha(picture.pixel(2, 3)) == 255);
}
