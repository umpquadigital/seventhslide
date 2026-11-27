// The output window's picture texture (PictureTexture), through Qt's Null
// graphics backend: the texture is kept while pictures fit it, made in the
// format Qt's own image texture picks, and each picture is let go once it is
// uploaded. The Null backend keeps no pixels, so what a window shows through
// it is test_presenter_view.cpp's, which compares it with the presenter's own
// picture when run on a graphics API
// (SEVENTHSLIDE_TEST_WEBENGINE=1 QT_QPA_PLATFORM=xcb xvfb-run -a ...).

#include <catch2/catch_test_macros.hpp>

#include "test_qt_app.hpp"

#if SEVENTHSLIDE_RENDER_RHI

#include <seventhslide/render/picture_texture.hpp>

#include <rhi/qrhi.h>

#include <QColor>
#include <QImage>
#include <QSize>

#include <memory>

using seventhslide::render::PictureTexture;

namespace {

/// A picture of ``size`` in ``format``, in a grey of ``tint``.
[[nodiscard]] QImage picture(QSize size, int tint,
                             QImage::Format format = QImage::Format_ARGB32_Premultiplied)
{
    QImage image(size, format);
    image.fill(QColor(tint, tint, tint));
    return image;
}

/// Commit ``texture`` as the renderer does in a frame.
void commit(QRhi& rhi, PictureTexture& texture)
{
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi.beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* batch = rhi.nextResourceUpdateBatch();
    texture.commitTextureOperations(&rhi, batch);
    cb->resourceUpdate(batch);
    REQUIRE(rhi.endOffscreenFrame() == QRhi::FrameOpSuccess);
}

}  // namespace

TEST_CASE("A window's picture texture is kept while pictures fit it",
          "[render][window][texture]")
{
    seventhslide::test::ensure_qt_application();
    QRhiNullInitParams params;
    const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &params));
    REQUIRE(rhi != nullptr);
    REQUIRE(rhi->isTextureFormatSupported(QRhiTexture::BGRA8));

    // The compositor's pictures: premultiplied ARGB, which is BGRA in memory
    // here and goes up as it is, in the format Qt's own image texture picks.
    const QSize size(64, 32);
    const QImage first = picture(size, 40);
    PictureTexture texture(first);
    CHECK(texture.textureSize() == size);
    CHECK(texture.hasAlphaChannel());
    CHECK_FALSE(texture.hasMipmaps());
    CHECK_FALSE(first.isDetached());  // held until it is uploaded

    commit(*rhi, texture);
    QRhiTexture* kept = texture.rhiTexture();
    REQUIRE(kept != nullptr);
    CHECK(kept->pixelSize() == size);
    CHECK(kept->format() == QRhiTexture::BGRA8);
    CHECK(first.isDetached());  // and let go once it is

    SECTION("the next picture goes into the same texture")
    {
        const QImage second = picture(size, 200);
        REQUIRE(texture.fits(second));
        texture.set_picture(second);
        commit(*rhi, texture);
        CHECK(texture.rhiTexture() == kept);
        CHECK(second.isDetached());
        // A frame with nothing new keeps the texture as it is.
        commit(*rhi, texture);
        CHECK(texture.rhiTexture() == kept);
    }

    SECTION("a picture of another size or format does not fit")
    {
        CHECK_FALSE(texture.fits(picture(QSize(65, 32), 40)));
        CHECK_FALSE(texture.fits(picture(size, 40, QImage::Format_RGB32)));
        CHECK(texture.fits(picture(size, 90)));
    }

    SECTION("a picture in another format goes up converted, as Qt's does")
    {
        PictureTexture other(picture(size, 70, QImage::Format_RGB888));
        commit(*rhi, other);
        REQUIRE(other.rhiTexture() != nullptr);
        CHECK(other.rhiTexture()->format() == QRhiTexture::RGBA8);
    }

    SECTION("an opaque picture is opaque, in the same format")
    {
        const QImage opaque = picture(size, 70, QImage::Format_RGB32);
        PictureTexture other(opaque);
        CHECK_FALSE(other.hasAlphaChannel());
        commit(*rhi, other);
        REQUIRE(other.rhiTexture() != nullptr);
        CHECK(other.rhiTexture()->format() == QRhiTexture::BGRA8);
    }
}

#endif
