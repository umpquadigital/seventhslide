#include "web_page_item.hpp"

#include "pointer_confinement.hpp"
#include "web_renderer.hpp"

#include <QCoreApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QWheelEvent>

#include <memory>

namespace seventhslide::app {
namespace {

/// The pointer the page hears from. Not the app's own mouse: Qt Quick keeps
/// one grab per pointer, not per window, so a press sent on with the app's
/// mouse would have the page's window take that mouse's grab from this item,
/// and the release would never come back here to be sent on (no click on the
/// page would ever finish).
const QPointingDevice* page_pointer()
{
    static QPointer<QPointingDevice> pointer;
    if (!pointer) {
        pointer = new QPointingDevice(
            QStringLiteral("SeventhSlide web page pointer"), 0x5e7e, QInputDevice::DeviceType::Mouse,
            QPointingDevice::PointerType::Generic,
            QInputDevice::Capability::Position | QInputDevice::Capability::Scroll
                | QInputDevice::Capability::Hover,
            1, 5, QString(), QPointingDeviceUniqueId(), QCoreApplication::instance());
    }
    return pointer;
}

} // namespace

WebPageItem::WebPageItem(QQuickItem* parent)
    : LiveFrameItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    // Clicking takes the page.
    setCursor(Qt::PointingHandCursor);
}

WebPageItem::~WebPageItem() = default;

QPointF WebPageItem::to_page(QPointF at) const
{
    if (width() <= 0 || height() <= 0) {
        return {};
    }
    return {at.x() * frame().width() / width(), at.y() * frame().height() / height()};
}

bool WebPageItem::forward(QEvent* event)
{
    auto* page = renderer();
    return page != nullptr && page->send_input(event);
}

bool WebPageItem::forward_mouse(QEvent::Type type, QSinglePointEvent* event)
{
    if (!interacting_) {
        event->ignore();
        return false;
    }
    const QPointF at = to_page(event->position());
    QMouseEvent out(type, at, at, at, event->button(), event->buttons(), event->modifiers(),
                    page_pointer());
    // When it happened, as the page would hear of any input.
    out.setTimestamp(event->timestamp());
    if (!forward(&out)) {
        event->ignore();
        return false;
    }
    event->accept();
    return true;
}

QRectF WebPageItem::area_in_window() const
{
    return mapRectToScene(QRectF(0, 0, width(), height()));
}

void WebPageItem::take(WebRenderer* page)
{
    interacting_ = true;
    taking_ = true;
    taken_ = page;
    unsetCursor();
    // Focus first: it takes the keys, and the pointer, from whatever had
    // them, which gives them up before this holds them.
    forceActiveFocus(Qt::MouseFocusReason);
    if (QQuickWindow* win = window()) {
        watched_ = win;
        win->installEventFilter(this);
        hold_pointer(*win);
    }
    emit interactingChanged();
}

void WebPageItem::hold_pointer(QQuickWindow& win)
{
    PointerConfinement* confinement = pointer_confinement();
    if (confinement == nullptr) {
        return;
    }
    held_area_ = area_in_window();
    hold_ = confinement->hold(win, held_area_);
    if (!hold_) {
        return;
    }
    // Let go by the system (the person broke the hold): the page is given
    // back. Queued: the hold says so from inside itself, and releasing drops
    // it.
    const quint64 serial = ++hold_serial_;
    connect(hold_.get(), &PointerHold::lost, this,
            [this, serial] {
                if (hold_ != nullptr && serial == hold_serial_) {
                    release();
                }
            },
            Qt::QueuedConnection);
    // Wherever the layout moves the page, the pointer is held on it: no
    // signal says an item moved on the window, but a move draws a frame.
    follow_ = connect(&win, &QQuickWindow::afterAnimating, this, &WebPageItem::follow_area);
}

void WebPageItem::follow_area()
{
    const QRectF area = area_in_window();
    if (hold_ != nullptr && area != held_area_) {
        held_area_ = area;
        hold_->move(area);
    }
}

void WebPageItem::release()
{
    if (!interacting_) {
        return;
    }
    interacting_ = false;
    taking_ = false;
    disconnect(follow_);
    hold_.reset();
    if (watched_) {
        watched_->removeEventFilter(this);
        watched_.clear();
    }
    setCursor(Qt::PointingHandCursor);
    // Nothing on the page stays hovered, and its caret stops blinking on the
    // outputs.
    if (WebRenderer* page = taken_.data()) {
        QEvent leave(QEvent::Leave);
        page->send_input(&leave);
        QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
        page->send_input(&out);
    }
    taken_.clear();
    emit interactingChanged();
}

bool WebPageItem::event(QEvent* event)
{
    // While the page has the keys, a key the app would take as a shortcut
    // (Space, B, F, the arrows, Escape) is the page's, or gives it back.
    if (event->type() == QEvent::ShortcutOverride && interacting_ && hasActiveFocus()) {
        event->accept();
        return true;
    }
    return QQuickItem::event(event);
}

bool WebPageItem::eventFilter(QObject* watched, QEvent* event)
{
    // A click anywhere else in the window (where the pointer is not held on
    // the page) gives it back.
    if (interacting_ && watched == watched_ && event->type() == QEvent::MouseButtonPress) {
        const auto* press = static_cast<QMouseEvent*>(event);
        if (!contains(mapFromScene(press->scenePosition()))) {
            release();
        }
    }
    return LiveFrameItem::eventFilter(watched, event);
}

void WebPageItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    // Never kept on a page no longer shown, or in a window it has left.
    if ((change == ItemVisibleHasChanged && !value.boolValue) || change == ItemSceneChange) {
        release();
    }
    LiveFrameItem::itemChange(change, value);
}

void WebPageItem::mousePressEvent(QMouseEvent* event)
{
    if (interacting_) {
        if (!forward_mouse(QEvent::MouseButtonPress, event)) {
            release();
        }
        return;
    }
    WebRenderer* page = renderer();
    if (page == nullptr) {
        event->ignore();
        return;
    }
    // This click takes the page; the page does not hear it.
    event->accept();
    take(page);
}

void WebPageItem::mouseMoveEvent(QMouseEvent* event)
{
    if (taking_) {
        event->accept();
        return;
    }
    forward_mouse(QEvent::MouseMove, event);
}

void WebPageItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (taking_) {
        taking_ = false;
        event->accept();
        // The page learns where the pointer is, and what it is over.
        const QPointF at = to_page(event->position());
        QMouseEvent move(QEvent::MouseMove, at, at, at, Qt::NoButton, Qt::NoButton,
                         event->modifiers(), page_pointer());
        move.setTimestamp(event->timestamp());
        forward(&move);
        return;
    }
    forward_mouse(QEvent::MouseButtonRelease, event);
}

void WebPageItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (taking_) {
        event->accept();
        return;
    }
    forward_mouse(QEvent::MouseButtonDblClick, event);
}

void WebPageItem::hoverMoveEvent(QHoverEvent* event)
{
    if (!interacting_) {
        return;
    }
    // The page sees the pointer pass over it (links, hover styles).
    const QPointF at = to_page(event->position());
    QMouseEvent out(QEvent::MouseMove, at, at, at, Qt::NoButton, Qt::NoButton, event->modifiers(),
                    page_pointer());
    out.setTimestamp(event->timestamp());
    forward(&out);
}

void WebPageItem::hoverLeaveEvent(QHoverEvent* /*event*/)
{
    if (!interacting_) {
        return;
    }
    // The pointer left the page: what it was over is no longer hovered.
    QEvent leave(QEvent::Leave);
    forward(&leave);
}

void WebPageItem::wheelEvent(QWheelEvent* event)
{
    if (!interacting_) {
        // Scrolling over a page not taken scrolls whatever is beneath it.
        event->ignore();
        return;
    }
    const QPointF at = to_page(event->position());
    QWheelEvent out(at, at, event->pixelDelta(), event->angleDelta(), event->buttons(),
                    event->modifiers(), event->phase(), event->inverted(), event->source(),
                    page_pointer());
    out.setTimestamp(event->timestamp());
    if (forward(&out)) {
        event->accept();
    } else {
        event->ignore();
    }
}

void WebPageItem::keyPressEvent(QKeyEvent* event)
{
    if (!interacting_) {
        event->ignore();
        return;
    }
    // Shift+Escape gives the page back; Escape alone is the page's (its
    // pop-ups, a video's full screen).
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::ShiftModifier) {
        event->accept();
        release();
        return;
    }
    const std::unique_ptr<QEvent> copy(event->clone());
    if (forward(copy.get())) {
        event->accept();
    } else {
        event->ignore();
    }
}

void WebPageItem::keyReleaseEvent(QKeyEvent* event)
{
    if (!interacting_) {
        event->ignore();
        return;
    }
    const std::unique_ptr<QEvent> copy(event->clone());
    if (forward(copy.get())) {
        event->accept();
    } else {
        event->ignore();
    }
}

void WebPageItem::focusOutEvent(QFocusEvent* event)
{
    // Something else has the keys (or the window lost them): the page is
    // given back rather than left listening to the pointer alone.
    release();
    LiveFrameItem::focusOutEvent(event);
}

} // namespace seventhslide::app
