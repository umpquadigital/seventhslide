#pragma once

// A web page drawn from its renderer's pictures and worked from there: once
// clicked into, a click, a scroll or a key goes to the page itself, as if it
// were the page. The page in a well of the Controller (LiveWebItem) is one.

#include "live_frame_item.hpp"

#include <QtQml/qqmlregistration.h>
#include <QEvent>
#include <QMetaObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>

#include <memory>

class QQuickWindow;

namespace seventhslide::app {

class PointerConfinement;
class PointerHold;
class WebRenderer;

/**
 * The page stretched to the item; its implicit size is the page's, so a
 * parent can fit it by aspect.
 *
 * Until it is clicked, the page hears nothing: a pointer passing over it or a
 * scroll over the column leaves it as it is. The click that takes the page
 * goes no further; from then on mouse, wheel and hover go to the page at the
 * same place on it, and keys go to it too, the app's own shortcuts included.
 * While the page is taken the windowing system holds the pointer on it
 * (pointer_confinement.hpp), wherever the layout moves it. Shift+Escape, the
 * item losing focus, being hidden, or the system letting the pointer go gives
 * it back, and so does a click anywhere else where the pointer is not held;
 * Escape alone is the page's.
 */
class WebPageItem : public LiveFrameItem {
    Q_OBJECT
    QML_ANONYMOUS
    /// The page is being worked from here: it has the pointer and the keys.
    Q_PROPERTY(bool interacting READ interacting NOTIFY interactingChanged)

public:
    explicit WebPageItem(QQuickItem* parent = nullptr);
    ~WebPageItem() override;

    [[nodiscard]] bool interacting() const noexcept { return interacting_; }

signals:
    void interactingChanged();

protected:
    /// The page's renderer; null when there is no page (input is ignored).
    [[nodiscard]] virtual WebRenderer* renderer() const { return nullptr; }
    /// What holds the pointer on the page while it is taken; null: nothing.
    [[nodiscard]] virtual PointerConfinement* pointer_confinement() const { return nullptr; }

    /// Give the page back: it hears nothing more until clicked again.
    void release();

    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    /// ``at`` (item pixels) on the page (page pixels).
    [[nodiscard]] QPointF to_page(QPointF at) const;
    /// While interacting, send ``event``'s button press, move or release to
    /// the page at the same place. False (and ``event`` ignored) otherwise.
    bool forward_mouse(QEvent::Type type, QSinglePointEvent* event);
    /// Send ``event`` to the page; false when there is none.
    bool forward(QEvent* event);
    /// Take ``page``: the keys, the pointer and what it does are its.
    void take(WebRenderer* page);
    /// Have the pointer held on the page, in ``win``.
    void hold_pointer(QQuickWindow& win);
    /// Hold the pointer where the page is now, if the layout moved it.
    void follow_area();
    /// The page's place on its window (its logical pixels).
    [[nodiscard]] QRectF area_in_window() const;

    bool interacting_ = false;
    /// The page taken: given back to, even once another is showing.
    QPointer<WebRenderer> taken_;
    /// The press that took the page is still down: its move and release are
    /// this item's, not the page's.
    bool taking_ = false;
    /// The window whose clicks are watched while the page is taken.
    QPointer<QQuickWindow> watched_;
    /// The pointer held on the page; null when it is not.
    std::unique_ptr<PointerHold> hold_;
    /// Where it is held, as last told.
    QRectF held_area_;
    /// Which hold ``lost`` speaks of: it comes queued, maybe after the next.
    quint64 hold_serial_ = 0;
    /// Each of the window's frames, while it is held.
    QMetaObject::Connection follow_;
};

} // namespace seventhslide::app
