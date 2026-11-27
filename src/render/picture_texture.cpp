#include <seventhslide/render/picture_texture.hpp>

#include <rhi/qrhi.h>

#include <QPixelFormat>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace seventhslide::render {

PictureTexture::PictureTexture(QImage picture)
    : picture_size_(picture.size())
    , picture_format_(picture.format())
    , size_(picture.size())
{
    picture_ = std::move(picture);
}

PictureTexture::~PictureTexture()
{
    delete texture_;
}

bool PictureTexture::fits(const QImage& picture) const noexcept
{
    return picture.size() == picture_size_ && picture.format() == picture_format_;
}

void PictureTexture::set_picture(QImage picture)
{
    picture_ = std::move(picture);
}

qint64 PictureTexture::comparisonKey() const
{
    // As Qt's: by the texture, and two not made yet are never the same.
    return texture_ != nullptr ? qint64(texture_) : qint64(this);
}

QRhiTexture* PictureTexture::rhiTexture() const
{
    return texture_;
}

QSize PictureTexture::textureSize() const
{
    return size_;
}

bool PictureTexture::hasAlphaChannel() const
{
    return QImage::toPixelFormat(picture_format_).alphaUsage() == QPixelFormat::UsesAlpha;
}

bool PictureTexture::hasMipmaps() const
{
    return false;
}

void PictureTexture::commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* updates)
{
    if (picture_.isNull()) {
        return;  // nothing new: the texture keeps the last picture
    }
    // Qt's image texture's steps (QSGPlainTexture::commitTextureOperations,
    // Qt 6.8), for a texture with no mipmaps that clamps at its edges.
    QImage upload;
    bool bgra = false;
    bool convert = false;
    if (picture_.format() == QImage::Format_RGB32
        || picture_.format() == QImage::Format_ARGB32_Premultiplied) {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        if (rhi->isTextureFormatSupported(QRhiTexture::BGRA8)) {
            upload = picture_;
            bgra = true;
        } else {
            convert = true;
        }
#else
        convert = true;
#endif
    } else if (picture_.format() == QImage::Format_RGBX8888
               || picture_.format() == QImage::Format_RGBA8888_Premultiplied) {
        upload = picture_;
    } else {
        convert = true;
    }
    if (convert) {
        upload = picture_.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }
    // Let go of the compositor's buffer, so it can paint into it again.
    picture_ = QImage();

    const int most = rhi->resourceLimit(QRhi::TextureSizeMax);
    if (upload.width() > most || upload.height() > most) {
        upload = upload.scaled(std::min(most, upload.width()), std::min(most, upload.height()),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    size_ = upload.size();

    const QRhiTexture::Format format = bgra ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8;
    if (texture_ != nullptr && (texture_->pixelSize() != size_ || texture_->format() != format)) {
        delete texture_;
        texture_ = nullptr;
    }
    if (texture_ == nullptr) {
        texture_ = rhi->newTexture(format, size_);
        if (!texture_->create()) {
            qWarning("Failed to build an output window's texture (size %dx%d)", size_.width(),
                     size_.height());
            delete texture_;
            texture_ = nullptr;
            return;
        }
    }
    if (upload.width() * 4 != upload.bytesPerLine()) {
        upload = upload.copy();
    }
    updates->uploadTexture(texture_, upload);
}

}  // namespace seventhslide::render
