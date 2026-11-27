#pragma once

// A picture from the session that changes many times a second, drawn
// stretched to the item: the live video's frame, a live web page's. Each new
// picture goes to the GPU once; the item's implicit size is the picture's, so
// a parent can fit it by aspect.

#include "session_bound_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QImage>

namespace seventhslide::app {

class LiveFrameItem : public SessionBoundItem {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit LiveFrameItem(QQuickItem* parent = nullptr);

protected:
    /// Show ``frame`` if it is a new picture (null: none, and no size).
    void show_frame(QImage frame);
    [[nodiscard]] const QImage& frame() const noexcept { return frame_; }

    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;

private:
    QImage frame_;
    /// ``frame_`` is not on the GPU yet.
    bool upload_ = false;
};

} // namespace seventhslide::app
