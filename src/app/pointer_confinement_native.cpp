#include "pointer_confinement_native.hpp"

#include <QWindow>
#include <QtMath>
#include <qpa/qplatformwindow.h>

namespace seventhslide::app {

QRect native_window_area(const QWindow& window, const QRectF& area)
{
    const QPlatformWindow* handle = window.handle();
    if (handle == nullptr) {
        return {};
    }
    // Qt's own scaling (QT_SCALE_FACTOR, or a screen's on X11 and Windows),
    // which the platform window's pixels are without; a Wayland surface's
    // buffer scale is in both, so not in this.
    const qreal scale = window.devicePixelRatio() / handle->devicePixelRatio();
    const int left = qCeil(area.left() * scale);
    const int top = qCeil(area.top() * scale);
    const int right = qFloor((area.left() + area.width()) * scale);
    const int bottom = qFloor((area.top() + area.height()) * scale);
    if (right <= left || bottom <= top) {
        return {};
    }
    return {left, top, right - left, bottom - top};
}

QRect native_screen_area(const QWindow& window, const QRectF& area)
{
    const QRect local = native_window_area(window, area);
    if (local.isEmpty()) {
        return {};
    }
    return local.translated(window.handle()->mapToGlobal(QPoint(0, 0)));
}

} // namespace seventhslide::app
