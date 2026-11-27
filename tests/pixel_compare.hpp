#pragma once

// Image comparisons for render tests, tolerant of anti-aliasing.

#include <QImage>
#include <QRect>

#include <cmath>

namespace seventhslide::test {

/// Whether each of ``px``'s channels is within ``tol`` of r, g and b.
[[nodiscard]] inline bool nearly_rgb(QRgb px, int r, int g, int b, int tol = 12) noexcept
{
    return std::abs(qRed(px) - r) <= tol && std::abs(qGreen(px) - g) <= tol
        && std::abs(qBlue(px) - b) <= tol;
}

[[nodiscard]] inline bool near_black(QRgb px, int tol) noexcept
{
    return qRed(px) <= tol && qGreen(px) <= tol && qBlue(px) <= tol;
}

/// Mean absolute difference per channel across all pixels (0 = identical).
[[nodiscard]] inline double mean_abs_diff(const QImage& a, const QImage& b)
{
    if (a.size() != b.size() || a.isNull() || b.isNull()) {
        return 1e9;
    }
    const QImage aa = a.convertToFormat(QImage::Format_ARGB32);
    const QImage bb = b.convertToFormat(QImage::Format_ARGB32);
    double sum = 0;
    const qint64 n = static_cast<qint64>(aa.width()) * aa.height() * 3;
    if (n <= 0) {
        return 0;
    }
    for (int y = 0; y < aa.height(); ++y) {
        for (int x = 0; x < aa.width(); ++x) {
            const QRgb pa = aa.pixel(x, y);
            const QRgb pb = bb.pixel(x, y);
            sum += std::abs(qRed(pa) - qRed(pb));
            sum += std::abs(qGreen(pa) - qGreen(pb));
            sum += std::abs(qBlue(pa) - qBlue(pb));
        }
    }
    return sum / static_cast<double>(n);
}

/// Ink count inside ``rect`` (clipped to image bounds): pixels with a channel
/// above ``tol``.
[[nodiscard]] inline int count_ink_in(const QImage& img, QRect rect, int tol = 8)
{
    if (img.isNull()) {
        return 0;
    }
    rect = rect.intersected(img.rect());
    int n = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            if (!near_black(img.pixel(x, y), tol)) {
                ++n;
            }
        }
    }
    return n;
}

/// Count pixels that are not near-black.
[[nodiscard]] inline int count_ink(const QImage& img, int tol = 8)
{
    return count_ink_in(img, img.rect(), tol);
}

}  // namespace seventhslide::test
