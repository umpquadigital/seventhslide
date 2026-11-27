#pragma once

// Packed YUV 4:2:2 encoders shared by the offscreen sinks.
//
// v4l2loopback wants YUYV - Y,U,Y,V in memory. DeckLink's bmdFormat8BitYUV
// ('2vuy') wants UYVY - U,Y,V,Y. Limited-range maths in BT.601 or BT.709,
// either byte order, so they are one loop with flags rather than encoders that
// can drift apart.
// Seam: Qt Gui only.

#include <QByteArray>
#include <QImage>

#include <cstdint>

namespace seventhslide::render {

/// Byte order of a packed 4:2:2 chroma pair.
enum class Yuv422Order {
    Yuyv,  ///< Y0,U,Y1,V - v4l2 V4L2_PIX_FMT_YUYV
    Uyvy,  ///< U,Y0,V,Y1 - DeckLink bmdFormat8BitYUV ('2vuy')
};

/// The colour matrix of a YUV picture.
enum class YuvMatrix {
    Bt601,  ///< SD
    Bt709,  ///< HD
};

/// The matrix video of ``height`` lines is in: BT.709 from 720 lines up,
/// BT.601 below. DeckLink's input and output both follow it.
[[nodiscard]] constexpr YuvMatrix video_yuv_matrix(int height) noexcept
{
    return height >= 720 ? YuvMatrix::Bt709 : YuvMatrix::Bt601;
}

/**
 * Pack ``image`` as 4:2:2 straight into caller memory.
 *
 * ``dst_stride`` is bytes per destination row, which is why this exists: a
 * DeckLink frame buffer pads its rows (IDeckLinkVideoFrame::GetRowBytes) and
 * must be written in place rather than packed tight and copied again.
 *
 * Width is forced even - 4:2:2 cannot carry an odd width. Returns false when
 * the image is unusable or ``dst_stride`` is too narrow for the row.
 */
[[nodiscard]] bool pack_yuv422(const QImage& image,
                               Yuv422Order order,
                               YuvMatrix matrix,
                               std::uint8_t* dst,
                               qsizetype dst_stride);

/// Tightly packed YUYV 4:2:2 (BT.601 limited). Width is forced even.
[[nodiscard]] QByteArray encode_yuyv(const QImage& image);

}  // namespace seventhslide::render
