#include <seventhslide/render/deinterlace.hpp>

#include <cstdint>

namespace seventhslide::render {

void deinterlace(QImage& picture)
{
    if (picture.depth() != 32 || picture.height() < 2) {
        return;
    }
    const int w = picture.width();
    const int h = picture.height();
    for (int y = 1; y < h; y += 2) {
        const auto* above = reinterpret_cast<const std::uint32_t*>(picture.constScanLine(y - 1));
        const auto* below = y + 1 < h
                                ? reinterpret_cast<const std::uint32_t*>(picture.constScanLine(y + 1))
                                : above;
        auto* line = reinterpret_cast<std::uint32_t*>(picture.scanLine(y));
        for (int x = 0; x < w; ++x) {
            // The mean of each byte, without carrying into the next.
            const std::uint32_t a = above[x];
            const std::uint32_t b = below[x];
            line[x] = (a & b) + (((a ^ b) & 0xfefefefeu) >> 1);
        }
    }
}

} // namespace seventhslide::render
