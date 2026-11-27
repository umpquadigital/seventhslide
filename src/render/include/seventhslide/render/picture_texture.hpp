#pragma once

// The texture an output window's view (PresenterView) shows its pictures by,
// on a graphics API: one texture kept while the pictures keep its size and
// format, each new picture uploaded into it, rather than a new texture for
// every picture. The software renderer draws only textures of its own, so
// there the view makes one per picture.
//
// It makes and fills its texture as Qt's own image texture does
// (QQuickWindow::createTextureFromImage, whose class is not public): the
// same format for the same picture, converted the same way, so the screen
// shows the same pixels.
//
// Built where Qt's GuiPrivate is (QRhi): SEVENTHSLIDE_RENDER_RHI.
//
// Seam: Qt Quick's scene graph. Render thread only: made and handed pictures
// in the view's updatePaintNode, uploaded when the renderer commits it.

#include <QImage>
#include <QSGTexture>
#include <QSize>

class QRhi;
class QRhiResourceUpdateBatch;
class QRhiTexture;

namespace seventhslide::render {

class PictureTexture final : public QSGTexture {
public:
    /// Its alpha is its pictures' format's, so ``picture`` is in one that
    /// says: not a palette (``QImage::Format_Indexed8``), whose colours do.
    explicit PictureTexture(QImage picture);
    ~PictureTexture() override;
    PictureTexture(const PictureTexture&) = delete;
    PictureTexture& operator=(const PictureTexture&) = delete;

    /// Whether ``picture`` can go into this texture: the same size and
    /// format as the first.
    [[nodiscard]] bool fits(const QImage& picture) const noexcept;
    /// Show ``picture`` (which ``fits``) from the next commit. Held until
    /// then, and let go once uploaded.
    void set_picture(QImage picture);

    [[nodiscard]] qint64 comparisonKey() const override;
    [[nodiscard]] QRhiTexture* rhiTexture() const override;
    [[nodiscard]] QSize textureSize() const override;
    [[nodiscard]] bool hasAlphaChannel() const override;
    /// None: the view asks for none.
    [[nodiscard]] bool hasMipmaps() const override;
    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* updates) override;

private:
    QImage picture_;
    QSize picture_size_;
    QImage::Format picture_format_;
    /// The texture's size: the picture's, unless the graphics take less.
    QSize size_;
    QRhiTexture* texture_ = nullptr;
};

}  // namespace seventhslide::render
