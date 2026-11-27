#include "pointer_confinement.hpp"

#include "pointer_confinement_native.hpp"

#include <QGuiApplication>
#include <QString>

namespace seventhslide::app {

void PointerHold::end()
{
    let_go();
    emit lost();
}

std::unique_ptr<PointerHold> PointerConfinement::hold(QWindow& window, const QRectF& area)
{
    // Two holds at once would fight (two clips, two sets of barriers), or be
    // refused outright (Wayland: a protocol error, which ends the app).
    if (current_) {
        current_->end();
    }
    std::unique_ptr<PointerHold> next = make_hold(window, area);
    current_ = next.get();
    return next;
}

std::unique_ptr<PointerConfinement> make_native_pointer_confinement()
{
    const QString platform = QGuiApplication::platformName();
#ifdef SEVENTHSLIDE_HAS_WAYLAND_CONFINEMENT
    // "wayland", "wayland-egl", ...
    if (platform.startsWith(QLatin1String("wayland"))) {
        return make_wayland_pointer_confinement();
    }
#endif
#ifdef SEVENTHSLIDE_HAS_X11_CONFINEMENT
    if (platform == QLatin1String("xcb")) {
        return make_x11_pointer_confinement();
    }
#endif
#ifdef SEVENTHSLIDE_HAS_WINDOWS_CONFINEMENT
    if (platform == QLatin1String("windows")) {
        return make_windows_pointer_confinement();
    }
#endif
    (void)platform;
    return nullptr;
}

} // namespace seventhslide::app
