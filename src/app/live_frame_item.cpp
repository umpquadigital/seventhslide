#include "live_frame_item.hpp"

#include <QQuickWindow>
#include <QSGImageNode>

#include <utility>

namespace seventhslide::app {

LiveFrameItem::LiveFrameItem(QQuickItem* parent)
    : SessionBoundItem(parent)
{
    setFlag(ItemHasContents, true);
}

void LiveFrameItem::show_frame(QImage frame)
{
    if (frame.cacheKey() == frame_.cacheKey()) {
        return;
    }
    frame_ = std::move(frame);
    upload_ = true;
    setImplicitSize(frame_.width(), frame_.height());
    update();
}

QSGNode* LiveFrameItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/)
{
    auto* node = static_cast<QSGImageNode*>(old);
    if (frame_.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        upload_ = true;
        return nullptr;
    }
    if (node == nullptr) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        // The picture is usually several times the item's size: mipmaps keep
        // the downscale smooth.
        node->setMipmapFiltering(QSGTexture::Linear);
        upload_ = true;
    }
    if (upload_) {
        node->setTexture(window()->createTextureFromImage(frame_, QQuickWindow::TextureHasMipmaps));
        upload_ = false;
    }
    node->setRect(boundingRect());
    return node;
}

} // namespace seventhslide::app
