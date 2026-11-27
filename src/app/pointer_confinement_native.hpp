#pragma once

// What the native pointer confinements share (pointer_confinement.hpp): an
// area of a window in the system's own pixels, and each system's maker.
// Built only where Qt's platform window (GuiPrivate) is at hand.

#include <QRect>
#include <QRectF>

#include <memory>

class QWindow;

namespace seventhslide::app {

class PointerConfinement;

/// ``area`` of ``window`` (its logical pixels) in its platform window's own
/// pixels (a Wayland surface's, X11's and Windows' device pixels), rounded
/// inward so every pixel in it is the area's; empty when nothing is left of
/// it, or the window has no platform window yet.
[[nodiscard]] QRect native_window_area(const QWindow& window, const QRectF& area);

/// The same, on the screen: X11's root window, Windows' virtual screen.
[[nodiscard]] QRect native_screen_area(const QWindow& window, const QRectF& area);

#ifdef SEVENTHSLIDE_HAS_WAYLAND_CONFINEMENT
/// Null when the compositor has no pointer constraints.
[[nodiscard]] std::unique_ptr<PointerConfinement> make_wayland_pointer_confinement();
#endif
#ifdef SEVENTHSLIDE_HAS_X11_CONFINEMENT
/// Null when the X server has no pointer barriers (XFixes 5).
[[nodiscard]] std::unique_ptr<PointerConfinement> make_x11_pointer_confinement();
#endif
#ifdef SEVENTHSLIDE_HAS_WINDOWS_CONFINEMENT
[[nodiscard]] std::unique_ptr<PointerConfinement> make_windows_pointer_confinement();
#endif

} // namespace seventhslide::app
