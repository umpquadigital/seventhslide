#include <seventhslide/render/pixel_pack.hpp>

#include <seventhslide/render/offscreen_sink.hpp>  // even_dim

#include <algorithm>

namespace seventhslide::render {
namespace {

/// RGB to limited-range YUV in 8.8 fixed point: a row each for Y, U and V.
struct Coefficients {
    int y[3];
    int u[3];
    int v[3];
};

constexpr Coefficients k_bt601{{66, 129, 25}, {-38, -74, 112}, {112, -94, -18}};
constexpr Coefficients k_bt709{{47, 157, 16}, {-26, -86, 112}, {112, -102, -10}};

/// One row of 4:2:2. Chroma is taken from the even pixel of each pair, which
/// is what both transports expect.
void pack_row(const QRgb* line, int src_width, int even_width, Yuv422Order order,
              const Coefficients& k, std::uint8_t* dst)
{
    const auto luma = [&k](int r, int g, int b) {
        return std::clamp(((k.y[0] * r + k.y[1] * g + k.y[2] * b + 128) >> 8) + 16, 0, 255);
    };
    for (int x = 0; x < even_width; x += 2) {
        const QRgb p0 = line[x];
        const QRgb p1 = line[std::min(x + 1, src_width - 1)];
        const int r0 = qRed(p0);
        const int g0 = qGreen(p0);
        const int b0 = qBlue(p0);
        const int r1 = qRed(p1);
        const int g1 = qGreen(p1);
        const int b1 = qBlue(p1);

        const int y0 = luma(r0, g0, b0);
        const int y1 = luma(r1, g1, b1);
        const int u =
            std::clamp(((k.u[0] * r0 + k.u[1] * g0 + k.u[2] * b0 + 128) >> 8) + 128, 0, 255);
        const int v =
            std::clamp(((k.v[0] * r0 + k.v[1] * g0 + k.v[2] * b0 + 128) >> 8) + 128, 0, 255);

        if (order == Yuv422Order::Yuyv) {
            *dst++ = static_cast<std::uint8_t>(y0);
            *dst++ = static_cast<std::uint8_t>(u);
            *dst++ = static_cast<std::uint8_t>(y1);
            *dst++ = static_cast<std::uint8_t>(v);
        } else {
            *dst++ = static_cast<std::uint8_t>(u);
            *dst++ = static_cast<std::uint8_t>(y0);
            *dst++ = static_cast<std::uint8_t>(v);
            *dst++ = static_cast<std::uint8_t>(y1);
        }
    }
}

[[nodiscard]] QByteArray encode_tight(const QImage& image, Yuv422Order order)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return {};
    }
    const int w = even_dim(image.width());
    const qsizetype stride = qsizetype(w) * 2;
    QByteArray out;
    out.resize(stride * qsizetype(image.height()));
    if (!pack_yuv422(image, order, YuvMatrix::Bt601, reinterpret_cast<std::uint8_t*>(out.data()), stride)) {
        return {};
    }
    return out;
}

}  // namespace

bool pack_yuv422(const QImage& image, Yuv422Order order, YuvMatrix matrix, std::uint8_t* dst,
                 qsizetype dst_stride)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 || dst == nullptr) {
        return false;
    }
    const int w = even_dim(image.width());
    if (dst_stride < qsizetype(w) * 2) {
        return false;
    }
    const QImage src = image.convertToFormat(QImage::Format_RGB32);
    if (src.isNull()) {
        return false;
    }
    const Coefficients& k = matrix == YuvMatrix::Bt709 ? k_bt709 : k_bt601;
    for (int y = 0; y < src.height(); ++y) {
        pack_row(reinterpret_cast<const QRgb*>(src.constScanLine(y)), src.width(), w, order, k,
                 dst + dst_stride * qsizetype(y));
    }
    return true;
}

QByteArray encode_yuyv(const QImage& image)
{
    return encode_tight(image, Yuv422Order::Yuyv);
}

}  // namespace seventhslide::render
