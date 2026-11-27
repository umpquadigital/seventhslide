#include <catch2/catch_test_macros.hpp>

#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/pixel_pack.hpp>

#include <QByteArray>
#include <QColor>
#include <QImage>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>

using seventhslide::render::bmd_uyvy_to_image;
using seventhslide::render::encode_yuyv;
using seventhslide::render::pack_yuv422;
using seventhslide::render::video_yuv_matrix;
using seventhslide::render::Yuv422Order;
using seventhslide::render::YuvMatrix;

namespace {

[[nodiscard]] QImage solid(int w, int h, QRgb color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    return img;
}

/// ``image`` packed UYVY with no row padding, the way the DeckLink sink packs.
[[nodiscard]] QByteArray uyvy(const QImage& image)
{
    const qsizetype stride = qsizetype(image.width() / 2) * 4;
    QByteArray out(stride * image.height(), '\0');
    if (!pack_yuv422(image, Yuv422Order::Uyvy, YuvMatrix::Bt601, reinterpret_cast<std::uint8_t*>(out.data()),
                     stride)) {
        return {};
    }
    return out;
}

}  // namespace

// The two orders exist because v4l2loopback wants YUYV and DeckLink's
// bmdFormat8BitYUV wants UYVY. Same maths, swapped bytes - which is exactly
// the kind of thing that silently produces a green picture if it drifts.
TEST_CASE("UYVY is YUYV with the pairs swapped", "[render][pixelpack]")
{
    REQUIRE(uyvy(QImage()).isEmpty());

    const QImage red = solid(2, 1, qRgb(255, 0, 0));
    const QByteArray yuyv = encode_yuyv(red);
    const QByteArray uyvy = ::uyvy(red);
    REQUIRE(yuyv.size() == 4);
    REQUIRE(uyvy.size() == 4);

    // YUYV: Y0 U Y1 V   ->   UYVY: U Y0 V Y1
    REQUIRE(uyvy[0] == yuyv[1]);
    REQUIRE(uyvy[1] == yuyv[0]);
    REQUIRE(uyvy[2] == yuyv[3]);
    REQUIRE(uyvy[3] == yuyv[2]);
}

TEST_CASE("UYVY packing writes BT.601 limited black", "[render][pixelpack]")
{
    const QByteArray uyvy = ::uyvy(solid(2, 1, qRgb(0, 0, 0)));
    REQUIRE(uyvy.size() == 4);
    REQUIRE(static_cast<unsigned char>(uyvy[0]) == 128);  // U
    REQUIRE(static_cast<unsigned char>(uyvy[1]) == 16);   // Y0
    REQUIRE(static_cast<unsigned char>(uyvy[2]) == 128);  // V
    REQUIRE(static_cast<unsigned char>(uyvy[3]) == 16);   // Y1
}

TEST_CASE("4:2:2 packing forces an even width", "[render][pixelpack]")
{
    // 4:2:2 shares chroma across a pair, so an odd width cannot be carried.
    const QByteArray odd = encode_yuyv(solid(3, 2, qRgb(0, 0, 0)));
    REQUIRE(odd.size() == 2 * 2 * 2);  // width floored to 2, two rows
}

// The stride-aware entry point is why this module exists: a DeckLink frame
// buffer pads its rows, so packing tight and copying again would be wrong as
// well as wasteful.
TEST_CASE("pack_yuv422 honours a padded destination stride", "[render][pixelpack]")
{
    const QImage red = solid(2, 2, qRgb(255, 0, 0));
    constexpr qsizetype k_stride = 16;  // 4 bytes of pixel, 12 of padding
    QByteArray buf(k_stride * 2, '\0');
    auto* dst = reinterpret_cast<std::uint8_t*>(buf.data());

    REQUIRE(pack_yuv422(red, Yuv422Order::Uyvy, YuvMatrix::Bt601, dst, k_stride));

    const QByteArray tight = uyvy(red);
    REQUIRE(tight.size() == 8);
    for (int row = 0; row < 2; ++row) {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(buf[k_stride * row + i] == tight[4 * row + i]);
        }
        // Padding past the row must be left alone, not written through.
        REQUIRE(buf[k_stride * row + 4] == '\0');
    }
}

TEST_CASE("pack_yuv422 refuses a stride too narrow for the row", "[render][pixelpack]")
{
    const QImage img = solid(4, 1, qRgb(0, 0, 0));
    QByteArray buf(64, '\0');
    auto* dst = reinterpret_cast<std::uint8_t*>(buf.data());
    REQUIRE_FALSE(pack_yuv422(img, Yuv422Order::Yuyv, YuvMatrix::Bt601, dst, 7));  // needs 8
    REQUIRE(pack_yuv422(img, Yuv422Order::Yuyv, YuvMatrix::Bt601, dst, 8));
    REQUIRE_FALSE(pack_yuv422(QImage(), Yuv422Order::Yuyv, YuvMatrix::Bt601, dst, 8));
    REQUIRE_FALSE(pack_yuv422(img, Yuv422Order::Yuyv, YuvMatrix::Bt601, nullptr, 8));
}

// What DeckLink sends as YUV comes back from a DeckLink input as it was: the
// output packs in the matrix the input decodes with, HD and SD alike.
TEST_CASE("DeckLink's YUV round-trips in its standard's matrix", "[render][pixelpack][bmd]")
{
    CHECK(video_yuv_matrix(1080) == YuvMatrix::Bt709);
    CHECK(video_yuv_matrix(720) == YuvMatrix::Bt709);
    CHECK(video_yuv_matrix(576) == YuvMatrix::Bt601);

    const QRgb colours[] = {qRgb(255, 0, 0),     qRgb(0, 255, 0),     qRgb(0, 0, 255),
                            qRgb(255, 255, 0),   qRgb(0, 255, 255),   qRgb(255, 0, 255),
                            qRgb(255, 255, 255), qRgb(0, 0, 0),       qRgb(128, 128, 128),
                            qRgb(200, 60, 30),   qRgb(40, 180, 90),   qRgb(70, 90, 210)};
    constexpr int k_band = 16;  // each colour a band of whole chroma pairs
    for (const int height : {1080, 576}) {
        const int width = height == 1080 ? 1920 : 720;
        QImage image(width, height, QImage::Format_RGB32);
        for (int y = 0; y < height; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                line[x] = colours[(x / k_band) % std::size(colours)];
            }
        }
        const int stride = width * 2;
        QByteArray packed(qsizetype(stride) * height, '\0');
        auto* dst = reinterpret_cast<std::uint8_t*>(packed.data());
        REQUIRE(pack_yuv422(image, Yuv422Order::Uyvy, video_yuv_matrix(height), dst, stride));
        const QImage back = bmd_uyvy_to_image(dst, width, height, stride);
        REQUIRE(back.size() == image.size());

        for (std::size_t i = 0; i < std::size(colours); ++i) {
            const QColor want(colours[i]);
            const QColor got = back.pixelColor(int(i) * k_band + 4, height / 2);
            INFO("height " << height << ", colour " << i << ": " << got.red() << ","
                           << got.green() << "," << got.blue());
            CHECK(std::abs(got.red() - want.red()) <= 2);
            CHECK(std::abs(got.green() - want.green()) <= 2);
            CHECK(std::abs(got.blue() - want.blue()) <= 2);
        }
    }

    // HD packed in BT.601 comes back as another colour.
    QImage red(1920, 1080, QImage::Format_RGB32);
    red.fill(qRgb(255, 0, 0));
    QByteArray packed(qsizetype(1920 * 2) * 1080, '\0');
    auto* dst = reinterpret_cast<std::uint8_t*>(packed.data());
    REQUIRE(pack_yuv422(red, Yuv422Order::Uyvy, YuvMatrix::Bt601, dst, 1920 * 2));
    CHECK(QColor(bmd_uyvy_to_image(dst, 1920, 1080, 1920 * 2).pixel(8, 8)).green() > 10);
}
